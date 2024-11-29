FROM ubuntu:20.04

# 安装Rust
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# 使用cargo和apt安装相关工具
RUN cargo install cargo-binutils \
    apt update && apt install -y \
    pkg-config \
    libclang-dev \
    qemu \
    qemu-kvm \
    qemu-system \
    virt-manager \
    bridge-utils

# 安装交叉编译工具链
RUN cd /usr/local/bin \
    wget https://musl.cc/aarch64-linux-musl-cross.tgz \
    wget https://musl.cc/riscv64-linux-musl-cross.tgz \
    wget https://musl.cc/x86_64-linux-musl-cross.tgz \
    tar zxf aarch64-linux-musl-cross.tgz \
    tar zxf riscv64-linux-musl-cross.tgz \
    tar zxf x86_64-linux-musl-cross.tgz \
    echo 'export PATH=/usr/local/bin/x86_64-linux-musl-cross/bin:/usr/local/bin/aarch64-linux-musl-cross/bin:/usr/local/bin/riscv64-linux-musl-cross/bin:$PATH' >> ~/.bashrc \
    source ~/.bashrc
