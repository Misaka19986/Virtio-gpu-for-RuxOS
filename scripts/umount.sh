#!/bin/sh

ROOTFS=../hvisor/images/aarch64/virtdisk/rootfs

umount $ROOTFS/proc
umount $ROOTFS/sys
umount $ROOTFS/dev/pts
umount $ROOTFS/dev
umount $ROOTFS/

echo "umount DONE"