#!/bin/bash
i=1;
index=1;
prefix='A9976300100';

MAX_INSERT_ROW_COUNT=$40;
while [ $i -le $MAX_INSERT_ROW_COUNT ]
do
    var=$(printf "%04d" "$index")
    uid=${prefix}${var};
    mysql -uulife -pgoscam66%%DB -h 127.0.0.1 remoteapp -e "insert into tx_device (DevID, GUID,SetupNo) values((select did.id from (select MAX(DevId) as id from tx_device) did) + 1, '${uid}', '${uid}');"
    d=$(date +%M-%d\ %H\:%m\:%S)
    echo "INSERT $i @@ $d"    
    i=$(($i+1))
    index=$(($index+1))
    sleep 0.05
done
exit 0
