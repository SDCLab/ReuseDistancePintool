#!/bin/bash
# find an already-running pid and monitor its memory access

ppid=$$
maxmem=0

#$@ <&0 > mem_output &
pid=`pgrep -n $1` # $! may work here but not later
while [[ ${pid} -ne "" ]]; do
    #mem=`ps v | grep "^[ ]*${pid}" | awk '{print $8}'`
        #the previous does not work with MPI
        mem=`cat /proc/${pid}/status | grep VmRSS | awk '{print $2}'`
    if [[ ${mem} -gt ${maxmem} ]]; then
        maxmem=${mem}
	echo "new max memory ${maxmem}"
    fi
    sleep 10
    savedpid=${pid}
    pid=`pgrep -n $1`
done
#wait ${savedpid} # don't wait, job is finished
#exitstatus=$?   # catch the exit status of wait, the same of $@
echo -e "Memory usage for $1 is: ${maxmem} KB. Exit status: ${exitstatus}\n"
