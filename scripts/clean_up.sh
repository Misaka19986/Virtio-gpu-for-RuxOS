#!/bin/sh

./hvisor zone shutdown -id 1
pkill hvisor-virtio
rmmod hvisor