#!/bin/sh

ROOTFS=../hvisor/images/aarch64/virtdisk/rootfs

update-binfmts --enable qemu-aarch64
chroot $ROOTFS