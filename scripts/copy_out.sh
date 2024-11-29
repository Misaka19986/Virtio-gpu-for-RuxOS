#!/bin/sh

ROOTFS=../hvisor/images/aarch64/virtdisk/rootfs
ZONEBASE=$ROOTFS/home/arm64

./mount.sh

if [ -f "$ZONEBASE/nohup.out" ]; then
    cp $ZONEBASE/nohup.out ../debug
fi

if [ -f "$ZONEBASE/log.txt" ]; then
    cp $ZONEBASE/log.txt ../debug
fi

# if [ -f "$ZONEBASE/shell.out" ]; then
#     cp $ZONEBASE/shell.out ../debug
# fi

./umount.sh