#! /bin/sh

for ((i=1; i<=$2; i++))  
do  
    user=$[$1+i]
    ./gssdemo -d 127.0.0.1:6001 -s 127.0.0.1:6000 -u $user -t signaling -b &
    echo "user $user" 
    sleep 0.1 
done


