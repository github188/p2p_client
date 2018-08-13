#! /bin/sh
rm -f *.log
for ((i=1; i<=$2; i++))  
do  
    user=test$[$1+i]
    ./gssdemo -d 127.0.0.1:6001 -s 127.0.0.1:7001 -u $user -t dispatch -b > $user.log &
    echo "user $user" 
    sleep 0.1 
done


