Create a Virtio-gpu for RuxOS by using Linux GPU Driver
This project is base on hvisor

### 如何启动root linux(Zone0)
假设平台为aarch64
首先需要交叉编译器aarch64-none-linux-gnu-*

```shell
wget https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
tar xvf gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
ls gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/
```

然后手动编译QEMU，推荐版本>8.1

```shell
# 安装编译所需的依赖包
sudo apt install autoconf automake autotools-dev curl libmpc-dev libmpfr-dev libgmp-dev \
              gawk build-essential bison flex texinfo gperf libtool patchutils bc \
              zlib1g-dev libexpat-dev pkg-config  libglib2.0-dev libpixman-1-dev libsdl2-dev \
              git tmux python3 python3-pip ninja-build
# 下载源码
wget https://download.qemu.org/qemu-8.2.7.tar.xz
# 解压
tar xvJf qemu-8.2.7.tar.xz
cd qemu-8.2.7
#生成设置文件
./configure --enable-kvm --enable-slirp --enable-debug --target-list=aarch64-softmmu,x86_64-softmmu
#编译
make -j$(nproc)
```

之后手动export来方便使用qemu-system-aarch64

```shell
export PATH=/path-to-qemu/qemu-8.2.7/build:$PATH
```

手动编译Linux kernel以获得镜像和驱动头文件

```shell
git clone https://github.com/torvalds/linux -b v5.4 --depth=1
cd linux
git checkout v5.4
# CROSS_COMPILE路径根据第一步安装交叉编译器的路径适当修改
# 导出默认配置
make ARCH=arm64 CROSS_COMPILE=/path-to-compiler/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- defconfig

# 在编译之前先修改根目录下的.config
# 在.config中增加一行
# CONFIG_BLK_DEV_RAM=y
# 修改.config的两个CONFIG参数
# CONFIG_IPV6=y
# CONFIG_BRIDGE=y
# .config较长，可以使用搜索来找到对应的设置

# 使用menuconfig来进一步配置
make ARCH=arm64 CROSS_COMPILE=/path-to-compiler/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- menuconfig

# 编译，CROSS_COMPILE路径根据第一步安装交叉编译器的路径适当修改
make ARCH=arm64 CROSS_COMPILE=/path-to-compiler/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- Image -j$(nproc)
```

生成的Linux镜像在arch/arm64/boot/Image，内核头文件分布在arch/arm64/include和include/

将生成的Image放入 /path-to-hvisor/hvisor/images/aarch64/kernel
同时这里需要一个文件系统镜像，见下文，文件系统镜像放 /path-to-hvisor/hvisor/images/aarch64/virtdisk

切换至根目录

```shell
make ARCH=aarch64 LOG=info FEATURES=platform_qemu run
```

编译成功后会进入uboot，此时再输入

```shell
bootm 0x40400000 - 0x40000000
```

从指定物理地址启动hvisor，此时会自动启动root linux
在制作镜像时，可以预先创建一个用户，否则将不支持shell的高级功能
若使用预先制作好的镜像，则可以

```shell
su arm64
```

### 如何在制作好的文件系统映像中增加生成的hvisor内核模块
首先需要拥有一个文件系统映像，可以使用已经制作好的映像
若需要自己制作，则需要先获得一个Linux某个发行版的最小系统，例如ubuntu 20.04 arm64 base，link: http://cdimage.ubuntu.com/ubuntu-base/releases/20.04/release/ubuntu-base-20.04.5-base-arm64.tar.gz

下载最小系统

```shell
wget http://cdimage.ubuntu.com/ubuntu-base/releases/20.04/release/ubuntu-base-20.04.5-base-arm64.tar.gz
```

之后创建一个ext4文件系统镜像

```shell
dd if=/dev/zero of=rootfs1.ext4 bs=1M count=1024 oflag=direct
mkfs.ext4 rootfs1.ext4
```

挂载这个文件系统镜像到某个文件夹下，例如rootfs/，并拷贝最小系统

```shell
mkdir rootfs
sudo mount -t ext4 rootfs1.ext4 rootfs/
sudo tar -xzf ubuntu-base-20.04.5-base-arm64.tar.gz -C rootfs/
```

或者

```shell
sudo mount -t ext4 /usr/local/Virt-GPU-for-RuxOS/hvisor/images/aarch64/virtdisk/rootfs1.ext4 rootfs/
```

让这个文件系统镜像内的操作系统获得一些当前物理机的信息，使得能够上网和使用dev，方便配置

```shell
# qemu-path为你的qemu路径
sudo cp qemu-path/build/qemu-system-aarch64 rootfs/usr/bin/
# 例如
sudo cp /usr/local/bin/qemu-8.2.7/build/qemu-system-aarch64 rootfs/usr/bin
# 配置域名解析
sudo cp /etc/resolv.conf rootfs/etc/resolv.conf
# 提供对进程和内核对象的访问
sudo mount -t proc /proc rootfs/proc
sudo mount -t sysfs /sys rootfs/sys
# 提供对设备的访问
sudo mount -o bind /dev rootfs/dev
sudo mount -o bind /dev/pts rootfs/dev/pts
```

切换到这个文件系统镜像内，安装一些必要的依赖

```shell
sudo apt-get install qemu-user-static
# 可能已经在内核中启动了
sudo update-binfmts --enable qemu-aarch64
# 可能会报错/usr/bin/groups: cannot find name for group ID xxxx，这是因为没有在镜像中注册和宿主操作系统一样的用户
sudo chroot rootfs/
sudo apt install dialog
sudo apt install git sudo vim bash-completion \
        kmod net-tools iputils-ping resolvconf ntpdate
```

Ubuntu base 20.04.5 arm64并没有init程序，需要手动安装
注: 若安装完整的systemd会导致启动较慢，但是有完整的shell和其他功能可用

```shell
apt install systemd systemd-sysv
```

并且做如下配置

```shell
# 在/lib/systemd/system/getty@.service中设置
ConditionPathExists=/dev/ttyAMA0
ExecStart=-/sbin/agetty -o '-p -- \\u' --keep-baud 115200,38400,9600 %I $TERM
DefaultInstance=ttyAMA0
# 启动相关服务，此时应该会在/etc/systemd/system/getty.target.want下生成一个软链接，如果没有也可以手动生成
systemctl enable getty@ttyAMA0.service
# 手动生成
ln -s /lib/systemd/system/getty@.service /etc/systemd/system/getty.target.want/getty@ttyAMA0.service
```

可以为文件系统镜像内的操作系统添加一些用户以及host

```shell
adduser arm64
adduser arm64 sudo
echo "kernel-5_4" > /etc/hostname
echo "127.0.0.1 localhost" > /etc/hosts
echo "127.0.0.1 kernel-5_4" >> /etc/hosts
passwd root
dpkg-reconfigure resolvconf
dpkg-reconfigure tzdata
```

退出并unmount

```shell
exit
sudo umount rootfs/proc
sudo umount rootfs/sys
sudo umount rootfs/dev/pts
sudo umount rootfs/dev
sudo umount rootfs/
```

当然，也可以先不卸载，我们可以先往镜像内拷贝hvisor的启动组件，包括hvisor的二进制工具，hvisor内核模块，要启动的ZoneN的操作系统镜像，以及该系统的设备树和.json配置文件

```shell
# path-to-hvisor为你的hvisor-tool目录
# 复制hvisor内核模块，下面的操作需要先编译hvisor-tool
# 目标路径是 mount-point/rootfs/home/arm64，可以全局搜索然后在脚本中更改，下同
sudo cp /path-to-hvisor-tool/hvisor-tool/driver/hvisor.ko rootfs/home/arm64
# 复制hvisor二进制文件
sudo cp /path-to-hvisor-tool/hvisor-tool/tools/hvisor rootfs/home/arm64
# 复制内核镜像，内核镜像的名字为Image
sudo cp /path-to-kernel-img/Image rootfs/home/arm64
# 复制设备树，在hvisor/images/aarch64/devicetree下make all生成
# 设备树必须命名为linux2.dtb
sudo cp /path-to-hvisor/hvisor/images/aarch64/devicetree/linux2.dtb rootfs/home/arm64
# 原地复制一份镜像(如果要启动linux，则复制一份rootfs1.ext4，命名为rootfs2.ext4)
sudo cp /path-to-hvisor/hvisor/images/aarch64/virtdisk/rootfs1.ext4 /path-to-hvisor/hvisor/images/aarch64/virtdisk/rootfs2.ext4
```

例

```shell
sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor/images/aarch64/kernel/Image rootfs/home/arm64
sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor-tool/driver/hvisor.ko rootfs/home/arm64
sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor-tool/tools/hvisor rootfs/home/arm64
sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor/images/aarch64/devicetree/linux2.dtb rootfs/home/arm64
# sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor/images/aarch64/virtdisk/rootfs2.ext4 rootfs/home/arm64
sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor-tool/examples/qemu-aarch64/virtio_cfg.json rootfs/home/arm64
sudo cp /usr/local/Virt-GPU-for-RuxOS/hvisor-tool/examples/qemu-aarch64/zone1_linux.json rootfs/home/arm64
```

### 编译virtio gpu所需的libdrm-dev
因为virtio gpu需要drm相关组件来控制显示设备，因此如果需要自己编译，则需要安装不同平台的libdrm-dev

以目标平台为arm64举例

```shell
# 首先下载libdrm
wget https://dri.freedesktop.org/libdrm/libdrm-2.4.100.tar.gz
tar -xzvf libdrm-2.4.100.tar.gz
cd libdrm-2.4.100
```

注: 2.4.100以上的版本需要较新版本的meson编译，因为笔者使用wsl2，其内核较旧，没办法安装最新的meson，故使用旧版

接下来配置configure
```shell
# 安装到你的aarch64-linux-gnu
# 为了方便可以这么安装，只不过不容易进行版本控制
./configure --host=aarch64-linux-gnu --prefix=/usr/aarch64-linux-gnu && make && make install

# 安装到其他文件夹，例如源码文件夹下的install
mkdir install
./configure --host=aarch64-linux-gnu --prefix=/path_to_install/install && make && make install

# 注意，prefix一定要是绝对路径
```

之后在你语言服务器启动时添加相关路径即可。在编译hvisor-tool时，需要修改tools文件夹下的makefile，让aarch64-linux-gnu-gcc知道这个include路径

### 语言服务器和VSC插件的使用
- **Rust**
  - 使用rust-analyzer即可
- **C**
  - 使用clangd(在编写hvisor-tool所生成的内核模块时不推荐)
    - 确保没有安装VSC的C/C++插件，也没有安装ccls插件
    - 确保安装了clangd
    - 在.vscode/settings.json中设置clangd路径
    - 在编写内核驱动时，clangd会扫描内核头文件，并且抛出很多错误，但是在编写常规C/CPP应用时仍然较为好用
    - 在.vscode/settings.json中设置include路径
  - 使用ccls
    - 确保安装了ccls插件
    - 确保安装了ccls
    - 在.vscode/settings.json中设置ccls路径
    - 配置根目录的.ccls文件来设置include路径
  - include路径
    - 包括所使用的Linux kernel镜像的include，以及Linux kernel目标arch下的include
    - 以及跨平台编译器的include，例如aarch64-linux-gnu
    - libdrm的include
    - hvisor-tool的各级include

同时，为你的语法服务器添加以下配置

"-DARM64",  // 由你的目标平台决定
"-D__KERNEL__",
"-DMODULE"

若使用clangd，则添加在settings.json中；若使用ccls，则添加在.ccls中