#!/bin/bash

CONTAINER_TESTDIR=/home/community

function usage() {
    echo "$0"
    echo -e "\t -d execution dir"
    echo -e "\t -c command"
    echo -e "\t -h help"
}

while getopts "d:c:h" opt; do
    case $opt in
        d)
            exec_dir=$OPTARG
            ;;
        c)
            cmd=$OPTARG
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

if [ -z "$exec_dir" ]; then
    usage
    exit 0
fi
if [ -z "$cmd" ]; then
    usage
    exit 0
fi

export PATH=$PATH:/home/debug/build/bin
ln -s  /home/debug/build/lib/libtaos.so /usr/lib/libtaos.so 2>/dev/null
mkdir -p /home/sim/tsim
mkdir -p /var/lib/taos/subscribe

cd $CONTAINER_TESTDIR/tests/$exec_dir
ulimit -c unlimited

$cmd
RET=$?

if [ $RET -ne 0 ]; then
    pwd
fi

exit $RET

