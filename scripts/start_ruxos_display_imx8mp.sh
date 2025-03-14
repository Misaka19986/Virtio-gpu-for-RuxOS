#!/bin/sh

rm ./nohup.out
rm ./log.txt

insmod hvisor.ko

stty -F /dev/ttymxc3 115200 ixoff

cat /dev/ttymxc3
