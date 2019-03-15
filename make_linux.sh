#/usr/bin/bash

HOSTLINUX="nagios@vlpsoout01-supsat10.si.cnaf.info"

rsync -azuv --exclude-from 'exclude-list.txt' * $HOSTLINUX:~/Src/LIB/

ssh $HOSTLINUX "cd ~/Src/LIB ; make clean ; make && cd ~/Src/LIB/examples && make clean ; make"

