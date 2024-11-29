#!/bin/sh

TARGET=${1:-all}
CONFIG=../config
VIRTDISK=../hvisor/images/aarch64/virtdisk
KERNEL=../hvisor/images/aarch64/kernel
HVISOR_TOOL=../hvisor-tool
ROOTFS=../hvisor/images/aarch64/virtdisk/rootfs
DEVICETREE=../hvisor/images/aarch64/devicetree
ZONEBASE=$ROOTFS/home/arm64
RUXOS=../ruxos
SCRIPTS=./

if [ ! -d "$VIRTDISK/rootfs" ]; then
    echo "mkdir rootfs"
    mkdir -p "$VIRTDISK/rootfs"
fi

# 挂载镜像
cp /etc/resolv.conf $ROOTFS/etc/resolv.conf
mount -t ext4 $VIRTDISK/rootfs1.ext4 $ROOTFS
mount -t proc /proc $ROOTFS/proc
mount -t sysfs /sys $ROOTFS/sys
mount -o bind /dev $ROOTFS/dev
mount -o bind /dev/pts $ROOTFS/dev/pts

# 拷贝hvisor-tool内核模块以及命令行工具
cp $HVISOR_TOOL/driver/hvisor.ko $ZONEBASE
cp $HVISOR_TOOL/tools/hvisor $ZONEBASE

# 拷贝所有配置
# 递归遍历CONFIG路径，把其中的每个文件拷贝到rootfs
find $CONFIG -type f | while read -r file; do
    cp "$file" "$ZONEBASE"
done

# 拷贝运行脚本
cp $SCRIPTS/start_linux.sh $ZONEBASE
cp $SCRIPTS/start_ruxos_display.sh $ZONEBASE
cp $SCRIPTS/start_ruxos_hello.sh $ZONEBASE
cp $SCRIPTS/clean_up.sh $ZONEBASE

# 根据参数进行拷贝
if [ "$TARGET" = "linux" ]; then
    echo "Target is linux"
    # 具体逻辑

    cp $DEVICETREE/linux2.dtb $ZONEBASE
    cp $KERNEL/Image $ZONEBASE
    # cp $VIRTDISK/rootfs2.ext4 $ZONEBASE

elif [ "$TARGET" = "ruxos_hello" ]; then
    echo "Target is ruxos_hello"
    # 具体逻辑
    
    cp $DEVICETREE/qemu-ruxos.dtb $ZONEBASE
    cp $RUXOS/apps/c/helloworld/helloworld_aarch64-qemu-virt.bin $ZONEBASE

elif [ "$TARGET" = "ruxos_display" ]; then
    echo "Target is ruxos_display"
    # 具体逻辑

    cp $DEVICETREE/qemu-ruxos.dtb $ZONEBASE
    cp $RUXOS/apps/display/basic_painting/basic_painting_aarch64-qemu-virt.bin $ZONEBASE

elif [ "$TARGET" = "all" ]; then
    echo "Target is all"
    # 具体逻辑

    cp $DEVICETREE/linux2.dtb $ZONEBASE
    cp $KERNEL/Image $ZONEBASE
    # cp $VIRTDISK/rootfs2.ext4 $ZONEBASE
    cp $DEVICETREE/qemu-ruxos.dtb $ZONEBASE
    cp $KERNEL/helloworld_aarch64-qemu-virt.bin $ZONEBASE
    cp $KERNEL/basic_painting_aarch64-qemu-virt.bin $ZONEBASE
fi

echo "DONE" 