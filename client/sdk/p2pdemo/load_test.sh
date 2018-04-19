#! /bin/sh

for ((i=1; i<=$2; i++))  
do  
    user=$[$1+i]
    ./p2pdemo -d 192.168.15.89:9999 -s 192.168.15.89:34780 -u $user -p $user &
    echo "count $user" 
    sleep 0.1 
done


