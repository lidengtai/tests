#!/bin/bash

# build TDengine in container

function usage() {
    echo "$0"
    echo -e "\t -m vm config file"
    echo -e "\t -b branch"
    echo -e "\t -l log dir"
    echo -e "\t -t build thread count"
    echo -e "\t -h help"
}

while getopts "m:b:l:t:h" opt; do
    case $opt in
        m)
            config_file=$OPTARG
            ;;
        b)
            branch=$OPTARG
            ;;
        l)
            log_dir=$OPTARG
            ;;
        t)
            build_thread_count=$OPTARG
            ;;
        h)
            usage
            exit 0
            ;;
        \?)
            echo "Invalid option: -$OPTARG"
            usage
            exit 0
            ;;
    esac
done
if [ -z $config_file ]; then
    usage
    exit 1
fi
if [ ! -f $config_file ]; then
    echo "$config_file not found"
    usage
    exit 1
fi
if [ -z $build_thread_count ]; then
    build_thread_count=1
fi

hosts=()
usernames=()
passwords=()
workdirs=()
threads=()

i=0
while [ 1 ]; do
    host=`jq .[$i].host $config_file`
    if [ "$host" = "null" ]; then
        break
    fi
    username=`jq .[$i].username $config_file`
    if [ "$username" = "null" ]; then
        break
    fi
    password=`jq .[$i].password $config_file`
    if [ "$password" = "null" ]; then
        password=""
    fi
    workdir=`jq .[$i].workdir $config_file`
    if [ "$workdir" = "null" ]; then
        break
    fi
    thread=`jq .[$i].thread $config_file`
    if [ "$thread" = "null" ]; then
        break
    fi
    hosts[i]=`echo $host|sed 's/\"$//'|sed 's/^\"//'`
    usernames[i]=`echo $username|sed 's/\"$//'|sed 's/^\"//'`
    passwords[i]=`echo $password|sed 's/\"$//'|sed 's/^\"//'`
    workdirs[i]=`echo $workdir|sed 's/\"$//'|sed 's/^\"//'`
    threads[i]=$thread
    i=$(( i + 1 ))
done

# build source
function build_src() {
    local index=$1
    echo "build source $index"
    local ssh_script="sshpass -p ${passwords[index]} ssh -o StrictHostKeyChecking=no ${usernames[index]}@${hosts[index]}"
    if [ -z ${passwords[index]} ]; then
        ssh_script="ssh -o StrictHostKeyChecking=no ${usernames[index]}@${hosts[index]}"
    fi
    local script="${workdirs[index]}/TDinternal/community/tests/parallel_test/container_build.sh -w ${workdirs[index]} -t ${build_thread_count}"
    local cmd="${ssh_script} ${script}"
    echo -e "\e[33m >>>>> \e[0m `date` ${hosts[index]} building ..."
    local start_time=`date +%s`
    ${cmd} >$log_dir/build.${hosts[index]}.stdout.log 2>$log_dir/build.${hosts[index]}.stderr.log
    local ret=$?
    local end_time=`date +%s`
    echo "${hosts[index]} build time: $(( end_time - start_time ))s" >>$log_dir/build.${hosts[index]}.stdout.log
    echo "${hosts[index]} build time: $(( end_time - start_time ))s"
    if [ $ret -ne 0 ]; then
        flock -x $lock_file -c "echo \"${hosts[index]} TDengine build\" >>$log_dir/failed.log"
        echo "======================================${hosts[index]}====================="
        cat $log_dir/build.${hosts[index]}.stderr.log
        echo "=========================================================================="
        return
    fi
}

function rename_taosdemo() {
    local index=$1
    local ssh_script="sshpass -p ${passwords[index]} ssh -o StrictHostKeyChecking=no ${usernames[index]}@${hosts[index]}"
    if [ -z ${passwords[index]} ]; then
        ssh_script="ssh -o StrictHostKeyChecking=no ${usernames[index]}@${hosts[index]}"
    fi
    local script="cp -rf ${workdirs[index]}/TDinternal/debug/build/bin/taosBenchmark ${workdirs[index]}/TDinternal/debug/build/bin/taosdemo 2>/dev/null"
    cmd="${ssh_script} sh -c \"$script\""
    ${cmd}
}

date_tag=`date +%Y%m%d-%H%M%S`
if [ -z $log_dir ]; then
    log_dir="log/build_${branch}_${date_tag}"
else
    log_dir="$log_dir/build_${branch}_${date_tag}"
fi

mkdir -p $log_dir
rm -rf $log_dir/*
lock_file=$log_dir/$$.lock

i=0
while [ $i -lt ${#hosts[*]} ]; do
    build_src $i &
    i=$(( i + 1 ))
done
wait

i=0
while [ $i -lt ${#hosts[*]} ]; do
    rename_taosdemo $i &
    i=$(( i + 1 ))
done
wait

i=0
if [ -f "$log_dir/failed.log" ]; then
    echo "====================================================="
    while read line; do
        echo -e "$i. $line \e[31m failed\e[0m" >&2
        i=$(( i + 1 ))
    done <$log_dir/failed.log
    RET=1
fi

echo "${log_dir}" >&2

date

exit $RET
