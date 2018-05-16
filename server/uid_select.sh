#!/bin/bash

args=$#
uid=$1
uidlen=${#uid}
if [ ${args} -lt 0 ]; then
        echo "parameter error"
        exit 0
fi

if [ ${uidlen} -eq 15 ]; then
        result="$(mysql -uulife -pgoscam66%%DB -h 127.0.0.1 remoteapp -e "select count(*) from tx_device where GUID='${uid}';")"
        count=`echo ${result} | awk -F' ' '{print $2}'`
	echo "select ${uid} done count=${count}"
else
        echo "uid length error ${uidlen}"
fi

exit 0
