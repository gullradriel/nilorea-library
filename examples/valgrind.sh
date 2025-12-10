#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./

# Please note that due to redhat's outdated glib & co we have to use a severe valgrind warning suppressor file. Took from https://github.com/google/mysql/blob/master/mysql-test/valgrind.supp as it is because we ancounter nearly all the cases

most_critical=0

OUT=`which valgrind &>/dev/null`
RET=$?

if [ $RET != 0 ]
then
    echo "valgrind binary not found => skipping tests"
    exit 0
fi

LOCKFILE="`basename $0`.lock"
LOCKTIMEOUT=60

# Create the lockfile.
touch $LOCKFILE

function valgrind_test {
	proc=$1
	params=$2
	procfile=${proc}_applog$3.txt
	valgrind_log=${proc}_debugmem$3.txt

	PROC="valgrind --suppressions=valgrind-suppressions.cfg --leak-check=full --show-leak-kinds=all --show-reachable=yes --run-libc-freeres=no --verbose --log-file=$valgrind_log ./$proc $params -V LOG_DEBUG 2>&1"
	echo $PROC
	PROC_OUTPUT=`$PROC`
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
	if [ $PROC_RET -gt $most_critical ]
	then
		most_critical=$PROC_RET
		echo "ERROR $proc returned $PROC_RET"
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
	if ! flock -u $FD
	then
  		echo "`basename $0`: failed to unlock $FD"
  		exit 1
	fi

	return $PROC_RET
}

valgrind_test "ex_base64_encode" "../README.md"
valgrind_test "ex_common"
valgrind_test "ex_log"
valgrind_test "ex_list"
valgrind_test "ex_hash"
valgrind_test "ex_trees"
valgrind_test "ex_stack"
valgrind_test "ex_exceptions"
valgrind_test "ex_nstr"
valgrind_test "ex_configfile" "CONFIGS/ex_configfile.cfg"
valgrind_test "ex_crypto" "-i ex_common"
#valgrind_test "ex_gui_netclient"
#valgrind_test "ex_gui_netserver"
#valgrind_test "ex_gui_particles"
#valgrind_test "ex_monolith"
valgrind_test "ex_pcre" '"TEST(.*)TEST" "TESTazerty1234TEST"'
#valgrind_test "ex_signals"
valgrind_test "ex_threads"

echo "#### NETWORK TESTING client ####"
./ex_network -a localhost -p 6666 &
sleep 1
valgrind_test "ex_network" "-s localhost -p 6666"

rm $LOCKFILE
rm ex_*.txt

exit $most_critical

