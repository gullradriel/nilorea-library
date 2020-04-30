#!/bin/bash

# Please note that due to redhat's outdated glib & co we have to use a severe valgrind warning suppressor file. Took from https://github.com/google/mysql/blob/master/mysql-test/valgrind.supp as it is because we ancounter nearly all the cases

most_critical=0

LOCKFILE="`basename $0`.lock"
LOCKTIMEOUT=60

# Create the lockfile.
touch $LOCKFILE

function valgrind_test {
	proc=$1
	params=$2
	procfile=${proc}_applog$3.txt
	valgrind_log=${proc}_debugmem$3.txt
	
	PROC_OUTPUT=`valgrind --suppressions=valgrind-suppressions.cfg --leak-check=full --show-leak-kinds=all --show-reachable=yes --run-libc-freeres=no --verbose --log-file=$valgrind_log ./$proc $params -V LOG_DEBUG 2>&1`
	PROC_RET=$?

	# Create a file descriptor over the given lockfile.
	exec {FD}<>$LOCKFILE
	if ! flock -x -w $LOCKTIMEOUT $FD
	then
  		echo "Failed to obtain a lock within $LOCKTIMEOUT seconds"
  		echo "Another instance of `basename $0` is probably running."
  		exit 1
	fi

	echo "##########"
	echo "Running $proc $params returned $PROC_RET"
	echo "$PROC_OUTPUT" > $procfile
	if [ $PROC_RET -ne 0 ]
	then
		echo "AppliLog:"
		cat $procfile
	fi

	TEST_OUTPUT=`grep "ERROR SUMMARY: 0 errors" $valgrind_log`
	RET=$?
	if [ $RET -ne 0 ]
	then
		echo "Memory test KO for $proc! "
		echo "AppliLog:"
		cat $procfile
		echo "ValgrindLog:"
		cat $valgrind_log 
	else
		echo "Memory test OK for $proc."
		rm $valgrind_log
	fi
	if [ $RET -gt $most_critical ]
	then
		most_critical=$PROC_RET	
	fi
	if ! flock -u $FD
	then
  		echo "`basename $0`: failed to unlock $FD"
  		exit 1
	fi

	return $PROC_RET
}

#valgrind_test "ex_log" 
valgrind_test "ex_list"  
valgrind_test "ex_nstr"  
valgrind_test "ex_common"  
valgrind_test "ex_log"  
valgrind_test "ex_exceptions"  
valgrind_test "ex_hash"  
valgrind_test "ex_pcre" '"TEST(.*)TEST" "TESTazerty1234TEST"'  
valgrind_test "ex_configfile" "ex_configfile.cfg"
echo "#### NETWORK TESTING ####" 
./ex_network -a localhost -p 6666 &
sleep 1
valgrind_test "ex_network" "-s localhost -p 6666"
#valgrind_test "ex_gui_particles"  
valgrind_test "ex_monolith"  

rm $LOCKFILE

exit $most_critical

