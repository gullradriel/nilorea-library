#!/bin/bash

if [ $# -eq 0 ]
  then
    echo "Usage: ./$0 <variable name> <string to encode>"
    exit 1
fi

name=$1
str=$2
str_len=$((${#str}))
var_len=$((${#str}+1))

echo "char $name[$var_len]=\"\";"

printf "N_HIDE_STR($name,"

for i in $(seq 1 $str_len)
do
	printf "'${str:i-1:1}'"
	if [[ $i != $str_len ]]; then
		printf ", "
	fi
done

printf ",'\\\0');\n"
