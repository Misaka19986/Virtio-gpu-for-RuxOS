#!/bin/sh

rm ./nohup.out
rm ./log.txt

insmod hvisor.ko
nohup ./hvisor virtio start virtio_cfg_ruxos.json & sleep 1
./hvisor zone start zone1_ruxos_hello.json