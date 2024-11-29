#ifndef __HVISOR_H
#define __HVISOR_H
#include <linux/ioctl.h>
#include <linux/types.h>

#include "def.h"
#include "zone_config.h"

#define MMAP_SIZE 4096
#define MAX_REQ 32
#define MAX_DEVS 4
#define MAX_CPUS 4 
#define MAX_ZONES MAX_CPUS

#define SIGHVI 10
// receive request from el2
// 某个zone试图访问其virtio设备的mmio区域，此时会引发缺页异常
// 之后zone会陷入hvisor，这里的device_req实际上是对mmio区域访问的request
// 由hvisor进行填写
struct device_req {
	__u64 src_cpu;	// 发生缺页异常的cpu
	__u64 address; // zone's ipa	// 异常地址
	__u64 size;	// 访问的大小
	__u64 value;	// 试图读/写的值
	__u32 src_zone;	// 来自哪个zone
	__u8 is_write;	// 是否是写操作
	__u8 need_interrupt;	// 是否需要中断kick zone
	__u16 padding;
};

struct device_res {
    __u32 target_zone;	// 设备所属的zone id
    __u32 irq_id;	// 设备的中断号
};

struct virtio_bridge {
	__u32 req_front;	// 请求队列头，由virtio设备维护(消费)
	__u32 req_rear;	// 请求队列尾，由hvisor维护(放入)
    __u32 res_front;	// 响应队列头，由hvisor维护(消费)
    __u32 res_rear;	// 响应队列尾，由virtio设备维护(放入)

	// 数据面结果队列(对应操作类的请求)
	struct device_req req_list[MAX_REQ];
    struct device_res res_list[MAX_REQ];

	// 控制面结果队列(对应查询和协调类的请求)
	// cfg_flags和cfg_values组成了控制面结果队列
	// 索引为CPU编号
	// flags存放控制面请求是否完成，value存放控制面交互结果
	// 这部分不需要virtio守护进程过度处理，因此zone cpu会阻塞在查询任务上，等待virtio守护进程放入结果
	__u64 cfg_flags[MAX_CPUS]; // avoid false sharing, set cfg_flag to u64
	__u64 cfg_values[MAX_CPUS];
	// TODO: When config is okay to use, remove these. It's ok to remove.
	__u64 mmio_addrs[MAX_DEVS];
	__u8 mmio_avail;
	__u8 need_wakeup;	// 是否需要唤醒守护进程
};

struct ioctl_zone_list_args {
	__u64 cnt;
	zone_info_t* zones;
};

typedef struct ioctl_zone_list_args zone_list_args_t;

#define HVISOR_INIT_VIRTIO    _IO(1, 0) // virtio device init
#define HVISOR_GET_TASK       _IO(1, 1)	
#define HVISOR_FINISH_REQ     _IO(1, 2)		  // finish one virtio req	
#define HVISOR_ZONE_START     _IOW(1, 3, zone_config_t*)
#define HVISOR_ZONE_SHUTDOWN  _IOW(1, 4, __u64)
#define HVISOR_ZONE_LIST      _IOR(1, 5, zone_list_args_t*)

#define HVISOR_HC_INIT_VIRTIO    0
#define HVISOR_HC_FINISH_REQ     1
#define HVISOR_HC_START_ZONE     2
#define HVISOR_HC_SHUTDOWN_ZONE  3
#define HVISOR_HC_ZONE_LIST      4

#endif /* __HVISOR_H */
