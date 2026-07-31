#!/bin/bash
#
# Nilorea Library
# Copyright (C) 2005-2026 Castagnier Mickael
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#

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
