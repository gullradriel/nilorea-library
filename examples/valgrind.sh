#!/bin/bash


# ALLEGRO enable test list
test_list="ex_configfile ex_exceptions ex_gui_particles ex_hash ex_list ex_monolith ex_network ex_nstr ex_pcre ex_threads"
# NO GFX list
test_list="ex_configfile ex_exceptions ex_hash ex_list ex_monolith ex_network ex_nstr ex_pcre ex_threads"

for example in `echo $test_list`
do
	echo " "
	echo "##########"
	echo "Testing $example"
	EXEMPLE_OUTPUT=`valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=${example}_debugmem.txt ./${example} -V LOG_DEBUG 2>/dev/null 1>/dev/null`
	EXEMPLE_RET=$?
	TEST_OUTPUT=`grep "ERROR SUMMARY: 0 errors from 0 contexts" ${example}_debugmem.txt`
	RET=$?
	echo "Running ${example} returned $EXEMPLE_RET"
	if [ $RET -ne 0 ]
	then
		echo "Memory test KO for $example! "
		echo "AppliLog:"
		echo $EXEMPLE_OUTPUT
		echo "ValgrindLog:"
		cat ${example}_debugmem.txt 
	else
		echo "Memory test OK for $example."
		rm ${example}_debugmem.txt
	fi
	echo "##########"
	echo " "
done
exit 0 
