/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "simInt.h"

SScript *simScriptList[MAX_MAIN_SCRIPT_NUM];
SCommand simCmdList[SIM_CMD_END];
int32_t  simScriptPos = -1;
int32_t  simScriptSucced = 0;
int32_t  simDebugFlag = 143;
void     simCloseTaosdConnect(SScript *script);
char     simScriptDir[PATH_MAX] = {0};

extern bool simExecSuccess;

int32_t simInitCfg() {
  SConfig *pCfg = cfgInit();
  if (pCfg == NULL) return -1;

  cfgAddDir(pCfg, "logDir", osLogDir());
  cfgAddBool(pCfg, "asyncLog", tsAsyncLog);
  cfgAddInt32(pCfg, "numOfLogLines", tsNumOfLogLines, 1000, 2000000000);
  cfgAddInt32(pCfg, "cDebugFlag", cDebugFlag, 0, 255);
  cfgAddInt32(pCfg, "uDebugFlag", uDebugFlag, 0, 255);
  cfgAddInt32(pCfg, "rpcDebugFlag", rpcDebugFlag, 0, 255);
  cfgAddInt32(pCfg, "tmrDebugFlag", tmrDebugFlag, 0, 255);
  cfgAddInt32(pCfg, "simDebugFlag", simDebugFlag, 0, 255);
  cfgAddInt32(pCfg, "debugFlag", 0, 0, 255);
  cfgAddString(pCfg, "scriptDir", configDir);

  char cfgFile[PATH_MAX + 100] = {0};
  taosExpandDir(cfgFile, configDir, PATH_MAX);
  snprintf(cfgFile, sizeof(cfgFile), "%s" TD_DIRSEP "taos.cfg", configDir);
  if (cfgLoad(pCfg, CFG_STYPE_CFG_FILE, cfgFile) != 0) {
    simError("failed to load from config file:%s since %s\n", cfgFile, terrstr());
    return -1;
  }

  osSetLogDir(cfgGetItem(pCfg, "logDir")->str);
  tsAsyncLog = cfgGetItem(pCfg, "asyncLog")->bval;
  tsNumOfLogLines = cfgGetItem(pCfg, "numOfLogLines")->i32;
  cDebugFlag = cfgGetItem(pCfg, "cDebugFlag")->i32;
  uDebugFlag = cfgGetItem(pCfg, "uDebugFlag")->i32;
  rpcDebugFlag = cfgGetItem(pCfg, "rpcDebugFlag")->i32;
  tmrDebugFlag = cfgGetItem(pCfg, "tmrDebugFlag")->i32;
  simDebugFlag = cfgGetItem(pCfg, "simDebugFlag")->i32;

  int32_t debugFlag = cfgGetItem(pCfg, "debugFlag")->i32;
  taosSetAllDebugFlag(debugFlag);

  tstrncpy(simScriptDir, cfgGetItem(pCfg, "scriptDir")->str, PATH_MAX);

  if (taosInitLog("simlog", 1) != 0) {
    simError("failed to init log file since %s\n", terrstr());
    cfgCleanup(pCfg);
    return -1;
  }

  cfgDumpCfg(pCfg);
  cfgCleanup(pCfg);
  return 0;
}

bool simSystemInit() {
  simInitCfg();
  simInitsimCmdList();
  memset(simScriptList, 0, sizeof(SScript *) * MAX_MAIN_SCRIPT_NUM);
  return true;
}

void simSystemCleanUp() {}

void simFreeScript(SScript *script) {
  if (script->type == SIM_SCRIPT_TYPE_MAIN) {
    simInfo("script:%s, background script num:%d, stop them", script->fileName, script->bgScriptLen);

    for (int32_t i = 0; i < script->bgScriptLen; ++i) {
      SScript *bgScript = script->bgScripts[i];
      simDebug("script:%s, is background script, set stop flag", bgScript->fileName);
      bgScript->killed = true;
      if (taosCheckPthreadValid(bgScript->bgPid)) {
        pthread_join(bgScript->bgPid, NULL);
      }

      simDebug("script:%s, background thread joined", bgScript->fileName);
      taos_close(bgScript->taos);
      tfree(bgScript->lines);
      tfree(bgScript->optionBuffer);
      tfree(bgScript);
    }

    simDebug("script:%s, is cleaned", script->fileName);
    taos_close(script->taos);
    tfree(script->lines);
    tfree(script->optionBuffer);
    tfree(script);
  }
}

SScript *simProcessCallOver(SScript *script) {
  if (script->type == SIM_SCRIPT_TYPE_MAIN) {
    simDebug("script:%s, is main script, set stop flag", script->fileName);
    if (script->killed) {
      simExecSuccess = false;
      simInfo("script:" FAILED_PREFIX "%s" FAILED_POSTFIX ", " FAILED_PREFIX "failed" FAILED_POSTFIX ", error:%s",
              script->fileName, script->error);
      return NULL;
    } else {
      simExecSuccess = true;
      simInfo("script:" SUCCESS_PREFIX "%s" SUCCESS_POSTFIX ", " SUCCESS_PREFIX "success" SUCCESS_POSTFIX,
              script->fileName);
      simCloseTaosdConnect(script);
      simScriptSucced++;
      simScriptPos--;

      simFreeScript(script);
      if (simScriptPos == -1) {
        simInfo("----------------------------------------------------------------------");
        simInfo("Simulation Test Done, " SUCCESS_PREFIX "%d" SUCCESS_POSTFIX " Passed:\n", simScriptSucced);
        return NULL;
      }

      return simScriptList[simScriptPos];
    }
  } else {
    simDebug("script:%s,  is stopped", script->fileName);
    simFreeScript(script);
    return NULL;
  }
}

void *simExecuteScript(void *inputScript) {
  SScript *script = (SScript *)inputScript;

  while (1) {
    if (script->type == SIM_SCRIPT_TYPE_MAIN) {
      script = simScriptList[simScriptPos];
    }

    if (abortExecution) {
      script->killed = true;
    }

    if (script->killed || script->linePos >= script->numOfLines) {
      script = simProcessCallOver(script);
      if (script == NULL) {
        simDebug("sim test abort now!");
        break;
      }
    } else {
      SCmdLine *line = &script->lines[script->linePos];
      char     *option = script->optionBuffer + line->optionOffset;
      simDebug("script:%s, line:%d with option \"%s\"", script->fileName, line->lineNum, option);

      SCommand *cmd = &simCmdList[line->cmdno];
      int32_t   ret = (*(cmd->executeCmd))(script, option);
      if (!ret) {
        script->killed = true;
      }
    }
  }

  simInfo("thread is stopped");
  return NULL;
}
