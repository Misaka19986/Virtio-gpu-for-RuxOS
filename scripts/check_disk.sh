#!/bin/sh

VIRTDISK=../hvisor/images/aarch64/virtdisk

fsck.ext4 -f $VIRTDISK/rootfs1.ext4