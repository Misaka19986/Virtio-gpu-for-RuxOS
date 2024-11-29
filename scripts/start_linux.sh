#!/bin/sh

rm ./nohup.out
rm ./log.txt

insmod hvisor.ko
nohup ./hvisor virtio start virtio_cfg_linux.json & ./hvisor zone start zone1_linux.json