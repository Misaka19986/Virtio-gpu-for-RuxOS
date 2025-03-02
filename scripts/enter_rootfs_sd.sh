#!/bin/sh

ROOTFS=/mnt/sd

update-binfmts --enable qemu-aarch64
chroot $ROOTFS