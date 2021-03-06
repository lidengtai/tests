###################################################################
#           Copyright (c) 2016 by TAOS Technologies, Inc.
#                     All rights reserved.
#
#  This file is proprietary and confidential to TAOS Technologies.
#  No part of this file may be reproduced, stored, transmitted,
#  disclosed or used in any form or by any means other than as
#  expressly provided by the written permission from Jianhui Tao
#
###################################################################

# -*- coding: utf-8 -*-

import sys
import time
import taos
from util.log import tdLog
from util.cases import tdCases
from util.sql import tdSql


class TDTestCase:
    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)

    def run(self):
        tbNum = 10
        rowNum = 20
        totalNum = tbNum * rowNum

        tdSql.prepare()

        tdLog.info("===== step1 =====")
        tdSql.execute(
            "create table stb0(ts timestamp, col1 int, col2 float) tags(tgcol int)")
        for i in range(tbNum):
            tdSql.execute("create table tb%d using stb0 tags(%d)" % (i, i))
            for j in range(rowNum):
                tdSql.execute(
                    "insert into tb%d values (now - %dm, %d, %d)" %
                    (i, 1440 - j, j, j))
        time.sleep(0.1)

        tdLog.info("===== step2 =====")
        tdSql.query("select count(col1) from tb0 interval(1d)")
        tdSql.checkData(0, 1, rowNum)
        tdSql.query("show tables")
        tdSql.checkRows(tbNum)
        tdSql.execute(
            "create table s0 as select count(col1) from tb0 interval(1d)")
        tdSql.query("show tables")
        tdSql.checkRows(tbNum + 1)

        tdLog.info("===== step3 =====")
        tdSql.waitedQuery("select * from s0", 1, 120)
        try:
            tdSql.checkData(0, 1, rowNum)
        except Exception as e:
            tdLog.info(repr(e))

        tdLog.info("===== step4 =====")
        tdSql.execute("drop table s0")
        tdSql.query("show tables")
        try:
            tdSql.checkRows(tbNum)
        except Exception as e:
            tdLog.info(repr(e))

        tdLog.info("===== step5 =====")
        tdSql.error("select * from s0")

        tdLog.info("===== step6 =====")
        tdSql.execute(
            "create table s0 as select count(*), count(col1), count(col2) from tb0 interval(1d)")
        tdSql.query("show tables")
        try:
            tdSql.checkRows(tbNum + 1)
        except Exception as e:
            tdLog.info(repr(e))

        tdLog.info("===== step7 =====")
        tdSql.waitedQuery("select * from s0", 1, 120)
        try:
            tdSql.checkData(0, 1, rowNum)
            tdSql.checkData(0, 2, rowNum)
            tdSql.checkData(0, 3, rowNum)
        except Exception as e:
            tdLog.info(repr(e))
        
        
        time.sleep(5)
        tdSql.query("show streams")
        tdSql.checkRows(1)
        tdSql.checkData(0, 2, 's0')

        tdLog.info("===== step8 =====")
        tdSql.query(
            "select count(*), count(col1), count(col2) from stb0 interval(1d)")
        try:
            tdSql.checkData(0, 1, totalNum)
            tdSql.checkData(0, 2, totalNum)
            tdSql.checkData(0, 3, totalNum)
        except Exception as e:
            tdLog.info(repr(e))
        tdSql.query("show tables")
        tdSql.checkRows(tbNum + 1)
        tdSql.execute(
            "create table s1 as select count(*), count(col1), count(col2) from stb0 interval(1d)")
        tdSql.query("show tables")
        tdSql.checkRows(tbNum + 2)

        tdLog.info("===== step9 =====")
        tdSql.waitedQuery("select * from s1", 1, 120)
        try:
            tdSql.checkData(0, 1, totalNum)
            tdSql.checkData(0, 2, totalNum)
            tdSql.checkData(0, 3, totalNum)
        except Exception as e:
            tdLog.info(repr(e))

        tdLog.info("===== step10 =====")
        tdSql.execute("drop table s1")
        tdSql.query("show tables")
        try:
            tdSql.checkRows(tbNum + 1)
        except Exception as e:
            tdLog.info(repr(e))

        tdLog.info("===== step11 =====")
        tdSql.error("select * from s1")

        tdLog.info("===== step12 =====")
        tdSql.execute(
            "create table s1 as select count(col1) from stb0 interval(1d)")
        tdSql.query("show tables")
        try:
            tdSql.checkRows(tbNum + 2)
        except Exception as e:
            tdLog.info(repr(e))

        tdLog.info("===== step13 =====")
        tdSql.waitedQuery("select * from s1", 1, 120)
        try:
            tdSql.checkData(0, 1, totalNum)
            #tdSql.checkData(0, 2, None)
            #tdSql.checkData(0, 3, None)
        except Exception as e:
            tdLog.info(repr(e))

        time.sleep(5)
        tdSql.query("show streams")
        tdSql.checkRows(2)
        tdSql.checkData(0, 2, 's1')
        tdSql.checkData(1, 2, 's0')
        tdSql.execute('kill stream %s ;' % tdSql.queryResult[0][0])
        time.sleep(5)
        tdSql.query("show streams")
        tdSql.checkRows(1)
        

    def stop(self):
        #tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
