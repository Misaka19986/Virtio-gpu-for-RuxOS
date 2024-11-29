### 编译hvisor-tool
需要gcc-aarch64-linux-gnu和libc6-dev-arm64-cross
后者提供编译必须的头文件

```shell
apt install gcc-aarch64-linux-gun libc6-dev-arm64-cross
```

同时需要下载交叉编译器aarch64-none-linux-gnu-

```shell
wget https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
tar xvf gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
ls gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/
```

可以export来方便编译

### hvisor-tool的调用链
因为hvisor-tool实在很临时，不好修改，因此在这里给出调用链(只给出hvisor-tool所实现的，系统库略过)

其主要有两个部分，包括守护进程和内核模块

守护进程负责接收和处理Zone0指令，大概有四种

- zone start
  - main(): hvisor.c
    - zone_start()
      - zone_start_from_json()
        - ioctl() -> 进入内核态，向hvisor调用函数，传递zone config和config size

- zone shutdown
  - main(): hvisor.c

- zone list
  - main(): hvisor.c

- virtio start
  - main(): hvisor.c
    - virtio_start()
      - virtio_init()
        - multithread_log_init()
        - initialize_log()
        - initialize_event_monitor()
      - virtio_start_from_json()
        - create_virtio_device_from_json()
          - create_virtio_device()
            - init_*_dev()
            - init_virtio_queue()
            - virtio_*_init()
      - handle_virtio_requests()
        - is_queue_empty()
        - virtio_handle_req()
          - virtio_mmio_write()/virtio_mmio_read()
          - virtio_finish_cfg_req() // 处理控制类命令时

### 相关配置文件的注意事项
注意，解析使用的cJSON库不支持json with comment

配置Zone的json文件:
- 需要添加ivc_configs字段，其是一个数组，用于虚拟机间通信
- 如果在ZoneN的dtb文件中启用了pcie设备，那么需要添加相关的pci_config、num_pci_devs、alloc_pci_devs字段

例如
```json
{
    "arch": "arm64",
    "name": "linux2",
    "zone_id": 1,
    "cpus": [
        2,
        3
    ],
    "memory_regions": [
        {
            "type": "ram",
            "physical_start": "0x50000000",
            "virtual_start": "0x50000000",
            "size": "0x30000000"
        },
        {
            "type": "virtio",
            "physical_start": "0xa003800",
            "virtual_start": "0xa003800",
            "size": "0x200"
        },
        {
            "type": "virtio",
            "physical_start": "0xa003c00",
            "virtual_start": "0xa003c00",
            "size": "0x200"
        }
    ],
    "interrupts": [
        75,
        76,
        78,
        35,
        36,
        37,
        38
    ],
    "ivc_configs": [],
    "kernel_filepath": "./Image",
    "dtb_filepath": "./linux2.dtb",
    "kernel_load_paddr": "0x50400000",
    "dtb_load_paddr": "0x50000000",
    "entry_point": "0x50400000",
    "arch_config": {
        "gicd_base": "0x8000000",
        "gicd_size": "0x10000",
        "gicr_base": "0x80a0000",
        "gicr_size": "0xf60000",
        "gits_base": "0x8080000",
        "gits_size": "0x20000"
    },
    "pci_config": {
        "ecam_base": "0x4010000000",
        "ecam_size": "0x10000000",
        "io_base": "0x3eff0000",
        "io_size": "0x10000",
        "pci_io_base": "0x0",
        "mem32_base": "0x10000000",
        "mem32_size": "0x2eff0000",
        "pci_mem32_base": "0x10000000",
        "mem64_base": "0x8000000000",
        "mem64_size": "0x8000000000",
        "pci_mem64_base": "0x8000000000"
    },
    "num_pci_devs": 1,
    "alloc_pci_devs": [0, 24]
}
```

配置virtio设备的json文件

例
```json
{
    "zones": [
        {
            "id": 1,
            "memory_region": [
                {
                    "zone0_ipa": "0x50000000",
                    "zonex_ipa": "0x50000000",
                    "size": "0x30000000"
                }
            ],
            "devices": [
                    {
                        "type": "blk",
                        "addr": "0xa003c00",
                        "len": "0x200",
                        "irq": 78,
                        "img": "rootfs2.ext4",
                        "status": "enable"
                    },
                    {
                        "type": "console",
                        "addr": "0xa003800",
                        "len": "0x200",
                        "irq": 76,
                        "status": "enable"
                    },
                    {
                        "type": "net",
                        "addr": "0xa003600",
                        "len": "0x200",
                        "irq": 75,
                        "tap": "tap0",
                        "mac": ["0x00", "0x16", "0x3e", "0x10", "0x10", "0x10"],
                        "status": "disable"
                    }
                ]
        }
    ]
}
```

dts、dtb文件
见hvisor/images/aarch64/devicetree下的dts、dtb文件

注意，如果没有在Zone配置文件中启用pcie设备，则需要把dts中pcie相关的部分注释掉并且再次编译