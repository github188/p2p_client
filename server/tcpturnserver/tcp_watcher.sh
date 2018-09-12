#! /bin/sh

host_dir=`echo ~`                               # ��ǰ�û���Ŀ¼ 
proc_name="tcpturnserver"                          # ������  
file_name="/tcpturnwatch.log"				# ��־�ļ�  
pid=0

proc_num()                                              # ���������  
{  
    num=`ps -ef | grep $proc_name | grep -v grep | wc -l`  
    return $num  
}

proc_id()                                               # ���̺�  
{  
    pid=`ps -ef | grep $proc_name | grep -v grep | awk '{print $2}'`  
}

while true; do

    proc_num
    number=$?
    if [ $number -eq 0 ]                                # �жϽ����Ƿ����  
    then   
        ulimit -c unlimited
        nohup /data/p2p_cp/tcpturnserver/tcpturnserver >/dev/null &           # �������̵��������Ӧ�޸�  
        proc_id                                         # ��ȡ�½��̺�  
        echo ${pid}, `date` >> $host_dir$file_name      # ���½��̺ź�����ʱ���¼  
    fi
    
    sleep 2           ###˯��2��ִ��һ��

done;
