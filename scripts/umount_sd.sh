#!/bin/sh

ROOTFS=/mnt/sd

umount $ROOTFS/proc
umount $ROOTFS/sys
umount $ROOTFS/dev/pts
umount $ROOTFS/dev
umount $ROOTFS/

echo "umount_sd DONE"