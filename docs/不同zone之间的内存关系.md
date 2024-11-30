### hvisor与zone0
hvisor与zone0唯一共用的内存是virtio-bridge，由处于内核态的hvisor.ko内核模块来进行管理，通过hvc来初始化

### zone0与zonex
virtio的启动配置virtio_cfg_*.json规定了所有zone的内存区域和使用的virtio设备