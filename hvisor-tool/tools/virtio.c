#include "virtio.h"
#include "cJSON.h"
#include "hvisor.h"
#include "log.h"
#include "virtio_blk.h"
#include "virtio_console.h"
#include "virtio_gpu.h"
#include "virtio_net.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

/// hvisor kernel module fd
int ko_fd;
volatile struct virtio_bridge *virtio_bridge;

pthread_mutex_t RES_MUTEX = PTHREAD_MUTEX_INITIALIZER;
VirtIODevice *vdevs[MAX_DEVS];
int vdevs_num;

// the index of `zone_mem[i]`
#define VIRT_ADDR 0
#define ZONE0_IPA 1
#define ZONEX_IPA 2
#define MEM_SIZE 3

#define MAX_RAMS 4
unsigned long long zone_mem[MAX_ZONES][MAX_RAMS][4];

#define WAIT_TIME 1000 // 1ms

const char *virtio_device_type_to_string(VirtioDeviceType type) {
    switch (type) {
    case VirtioTNone:
        return "virtio-none";
    case VirtioTNet:
        return "virtio-net";
    case VirtioTBlock:
        return "virtio-blk";
    case VirtioTConsole:
        return "virtio-console";
    case VirtioTGPU:
        return "virtio-gpu";
    default:
        return "unknown";
    }
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_error("fcntl(F_GETFL) failed");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("fcntl(F_SETFL) failed");
        return -1;
    }
    return 0;
}

int get_zone_ram_index(void *zonex_ipa, int zone_id) {
    for (int i = 0; i < MAX_RAMS; i++) {
        if (zone_mem[zone_id][i][MEM_SIZE] == 0)
            continue;
        ;
        if (zonex_ipa >= zone_mem[zone_id][i][ZONEX_IPA] &&
            zonex_ipa < zone_mem[zone_id][i][ZONEX_IPA] +
                            zone_mem[zone_id][i][MEM_SIZE]) {
            return i;
        }
    }
    log_error("can't find zone mem index for zonex ipa %#x", zonex_ipa);
    return -1;
}

inline int is_queue_full(unsigned int front, unsigned int rear,
                         unsigned int size) {
    if (((rear + 1) & (size - 1)) == front) {
        return 1;
    } else {
        return 0;
    }
}

inline int is_queue_empty(unsigned int front, unsigned int rear) {
    return rear == front;
}

/// Write barrier to make sure all write operations are finished before this
/// operation
inline void write_barrier(void) {
#ifdef ARM64
    asm volatile("dmb ishst" ::: "memory");
#endif
#ifdef RISCV64
    asm volatile("fence w,w" ::: "memory");
#endif
}

inline void read_barrier(void) {
#ifdef ARM64
    asm volatile("dmb ishld" ::: "memory");
#endif
#ifdef RISCV64
    asm volatile("fence r,r" ::: "memory");
#endif
}

inline void rw_barrier(void) {
#ifdef ARM64
    asm volatile("dmb ish" ::: "memory");
#endif
#ifdef RISCV64
    asm volatile("fence rw,rw" ::: "memory");
#endif
}

// create a virtio device.
VirtIODevice *create_virtio_device(VirtioDeviceType dev_type, uint32_t zone_id,
                                   uint64_t base_addr, uint64_t len,
                                   uint32_t irq_id, void *arg0, void *arg1) {
    log_info(
        "create virtio device type %s, zone id %d, base addr %lx, len %lx, "
        "irq id %d",
        virtio_device_type_to_string(dev_type), zone_id, base_addr, len,
        irq_id);
    VirtIODevice *vdev = NULL;
    int is_err;
    vdev = calloc(1, sizeof(VirtIODevice));
    init_mmio_regs(&vdev->regs, dev_type);
    vdev->base_addr = base_addr;
    vdev->len = len;
    vdev->zone_id = zone_id;
    vdev->irq_id = irq_id;
    vdev->type = dev_type;

    // 根据设备类型进行初始化
    switch (dev_type) {
    case VirtioTBlock:
        vdev->regs.dev_feature = BLK_SUPPORTED_FEATURES;
        vdev->dev = init_blk_dev(vdev);
        init_virtio_queue(vdev, dev_type);
        is_err = virtio_blk_init(vdev, (const char *)arg0);
        break;

    case VirtioTNet:
        vdev->regs.dev_feature = NET_SUPPORTED_FEATURES;
        vdev->dev = init_net_dev(arg0);
        init_virtio_queue(vdev, dev_type);
        is_err = virtio_net_init(vdev, (char *)arg1);
        break;

    case VirtioTConsole:
        vdev->regs.dev_feature = CONSOLE_SUPPORTED_FEATURES;
        vdev->dev = init_console_dev();
        init_virtio_queue(vdev, dev_type);
        is_err = virtio_console_init(vdev);
        break;

    case VirtioTGPU:
        vdev->regs.dev_feature = GPU_SUPPORTED_FEATURES;
        vdev->dev = init_gpu_dev((GPURequestedState *)arg0);
        free(arg0);
        init_virtio_queue(vdev, dev_type);
        is_err = virtio_gpu_init(vdev);
        break;

    default:
        log_error("unsupported virtio device type\n");
        goto err;
    }

    if (is_err)
        goto err;

    // If reaches max number of virtual devices
    if (vdevs_num == MAX_DEVS) {
        log_error("virtio device num exceed max limit");
        goto err;
    }

    if (vdev->dev == NULL) {
        log_error("failed to init dev");
        goto err;
    }

    log_info("create %s success", virtio_device_type_to_string(dev_type));
    vdevs[vdevs_num++] = vdev;

    return vdev;

err:
    free(vdev);
    return NULL;
}

void init_virtio_queue(VirtIODevice *vdev, VirtioDeviceType type) {
    VirtQueue *vqs = NULL;

    log_info("Initializing virtio queue for zone:%d, device type:%s",
             vdev->zone_id, virtio_device_type_to_string(type));

    switch (type) {
    case VirtioTBlock:
        vdev->vqs_len = 1;
        vqs = malloc(sizeof(VirtQueue));
        virtqueue_reset(vqs, 0);
        vqs->queue_num_max = VIRTQUEUE_BLK_MAX_SIZE;
        vqs->notify_handler = virtio_blk_notify_handler;
        vqs->dev = vdev;
        vdev->vqs = vqs;
        break;

    case VirtioTNet:
        vdev->vqs_len = NET_MAX_QUEUES;
        vqs = malloc(sizeof(VirtQueue) * NET_MAX_QUEUES);
        for (int i = 0; i < NET_MAX_QUEUES; ++i) {
            virtqueue_reset(vqs, i);
            vqs[i].queue_num_max = VIRTQUEUE_NET_MAX_SIZE;
            vqs[i].dev = vdev;
        }
        vqs[NET_QUEUE_RX].notify_handler = virtio_net_rxq_notify_handler;
        vqs[NET_QUEUE_TX].notify_handler = virtio_net_txq_notify_handler;
        vdev->vqs = vqs;
        break;

    case VirtioTConsole:
        vdev->vqs_len = CONSOLE_MAX_QUEUES;
        vqs = malloc(sizeof(VirtQueue) * CONSOLE_MAX_QUEUES);
        for (int i = 0; i < CONSOLE_MAX_QUEUES; ++i) {
            virtqueue_reset(vqs, i);
            vqs[i].queue_num_max = VIRTQUEUE_CONSOLE_MAX_SIZE;
            vqs[i].dev = vdev;
        }
        vqs[CONSOLE_QUEUE_RX].notify_handler =
            virtio_console_rxq_notify_handler;
        vqs[CONSOLE_QUEUE_TX].notify_handler =
            virtio_console_txq_notify_handler;
        vdev->vqs = vqs;
        break;

    case VirtioTGPU:
        vdev->vqs_len = GPU_MAX_QUEUES;
        vqs = malloc(sizeof(VirtQueue) * GPU_MAX_QUEUES);
        for (int i = 0; i < GPU_MAX_QUEUES; ++i) {
            virtqueue_reset(vqs, i);
            vqs[i].queue_num_max = VIRTQUEUE_GPU_MAX_SIZE;
            vqs[i].dev = vdev;
        }
        vqs[GPU_CONTROL_QUEUE].notify_handler = virtio_gpu_ctrl_notify_handler;
        vqs[GPU_CURSOR_QUEUE].notify_handler = virtio_gpu_cursor_notify_handler;
        vdev->vqs = vqs;
        break;

    default:
        break;
    }
}

void init_mmio_regs(VirtMmioRegs *regs, VirtioDeviceType type) {
    log_info("initializing mmio registers for %s",
             virtio_device_type_to_string(type));
    regs->device_id = type;
    regs->queue_sel = 0;
}

void virtio_dev_reset(VirtIODevice *vdev) {
    // When driver read first 4 encoded messages, it will reset dev.
    log_trace("virtio dev reset");
    vdev->regs.status = 0;
    vdev->regs.interrupt_status = 0;
    vdev->regs.interrupt_count = 0;
    int idx = vdev->regs.queue_sel;
    vdev->vqs[idx].ready = 0;
    for (uint32_t i = 0; i < vdev->vqs_len; i++) {
        virtqueue_reset(&vdev->vqs[i], i);
    }
    vdev->activated = false;
}

void virtqueue_reset(VirtQueue *vq, int idx) {
    // Reserve these fields
    void *addr = vq->notify_handler;
    VirtIODevice *dev = vq->dev;
    uint32_t queue_num_max = vq->queue_num_max;

    // Clear others
    memset(vq, 0, sizeof(VirtQueue));
    vq->vq_idx = idx;
    vq->notify_handler = addr;
    vq->dev = dev;
    vq->queue_num_max = queue_num_max;
    pthread_mutex_init(&vq->used_ring_lock, NULL);
}

// check if virtqueue has new requests
bool virtqueue_is_empty(VirtQueue *vq) {
    if (vq->avail_ring == NULL) {
        log_error("virtqueue's avail ring is invalid");
        return true;
    }
    // read_barrier();
    log_debug("vq->last_avail_idx is %d, vq->avail_ring->idx is %d",
              vq->last_avail_idx, vq->avail_ring->idx);
    if (vq->last_avail_idx == vq->avail_ring->idx)
        return true;
    else
        return false;
}

bool desc_is_writable(volatile VirtqDesc *desc_table, uint16_t idx) {
    if (desc_table[idx].flags & VRING_DESC_F_WRITE)
        return true;
    return false;
}

void *get_virt_addr(void *zonex_ipa, int zone_id) {
    int ram_idx = get_zone_ram_index(zonex_ipa, zone_id);
    return zone_mem[zone_id][ram_idx][VIRT_ADDR] -
           zone_mem[zone_id][ram_idx][ZONEX_IPA] + zonex_ipa;
}

// When virtio device is processing virtqueue, driver adding an elem to
// virtqueue is no need to notify device.
void virtqueue_disable_notify(VirtQueue *vq) {
    if (vq->event_idx_enabled) {
        VQ_AVAIL_EVENT(vq) = vq->last_avail_idx - 1;
    } else {
        vq->used_ring->flags |= (uint16_t)VRING_USED_F_NO_NOTIFY;
    }
    write_barrier();
}

void virtqueue_enable_notify(VirtQueue *vq) {
    if (vq->event_idx_enabled) {
        VQ_AVAIL_EVENT(vq) = vq->avail_ring->idx;
    } else {
        vq->used_ring->flags &= !(uint16_t)VRING_USED_F_NO_NOTIFY;
    }
    write_barrier();
}

void virtqueue_set_desc_table(VirtQueue *vq) {
    int zone_id = vq->dev->zone_id;
    log_debug("zone %d set dev %s desc table ipa at %#x", zone_id,
              virtio_device_type_to_string(vq->dev->type), vq->desc_table_addr);
    vq->desc_table = (VirtqDesc *)get_virt_addr(vq->desc_table_addr, zone_id);
}

void virtqueue_set_avail(VirtQueue *vq) {
    int zone_id = vq->dev->zone_id;
    log_debug("zone %d set dev %s avail ring ipa at %#x", zone_id,
              virtio_device_type_to_string(vq->dev->type), vq->avail_addr);
    vq->avail_ring = (VirtqAvail *)get_virt_addr(vq->avail_addr, zone_id);
}

void virtqueue_set_used(VirtQueue *vq) {
    int zone_id = vq->dev->zone_id;
    log_debug("zone %d set dev %s used ring ipa at %#x", zone_id,
              virtio_device_type_to_string(vq->dev->type), vq->used_addr);
    vq->used_ring = (VirtqUsed *)get_virt_addr(vq->used_addr, zone_id);
}

// record one descriptor to iov.
inline int descriptor2iov(int i, volatile VirtqDesc *vd, struct iovec *iov,
                          uint16_t *flags, int zone_id, bool copy_flags) {
    void *host_addr;
    host_addr = get_virt_addr((void *)vd->addr, zone_id);
    iov[i].iov_base = host_addr;
    iov[i].iov_len = vd->len;
    // log_debug("vd->addr ipa is %x, iov_base is %x, iov_len is %d", vd->addr,
    // host_addr, vd->len);
    if (copy_flags)
        flags[i] = vd->flags;
    return 0;
}

/// record one descriptor list to iov
/// \param desc_idx the first descriptor's idx in descriptor list.
/// \param iov the iov to record
/// \param flags each descriptor's flags
/// \param append_len the number of iovs to append
/// \return the len of iovs
int process_descriptor_chain(VirtQueue *vq, uint16_t *desc_idx,
                             struct iovec **iov, uint16_t **flags,
                             int append_len, bool copy_flags) {
    uint16_t next, last_avail_idx;
    volatile VirtqDesc *vdesc, *ind_table, *ind_desc;
    int chain_len = 0, i, table_len;

    // idx is the last available index processed during the last kick
    last_avail_idx = vq->last_avail_idx;

    // No new requests
    if (last_avail_idx == vq->avail_ring->idx)
        return 0;

    // Update to the index to be processed during this kick
    vq->last_avail_idx++;

    // Get the index of the first available descriptor
    *desc_idx = next = vq->avail_ring->ring[last_avail_idx & (vq->num - 1)];
    // Record the length of the descriptor chain to chain_len
    for (i = 0; i < (int)vq->num; i++, next = vdesc->next) {
        // Get a descriptor
        vdesc = &vq->desc_table[next];
        // TODO: vdesc->len may not be chain_len, virtio specification doesn't
        // say it.

        // Check if this descriptor supports the VRING_DESC_F_INDIRECT feature
        // If supported, it means that the descriptor points to a set of
        // descriptors, i.e., one descriptor can describe multiple scattered
        // buffers
        if (vdesc->flags & VRING_DESC_F_INDIRECT) {
            chain_len +=
                vdesc->len / 16; // This descriptor points to 16 descriptors
            i--;
        }
        // Exit if there is no next descriptor
        if ((vdesc->flags & VRING_DESC_F_NEXT) == 0)
            break;
    }

    // Update chain length and reset next to the first descriptor
    chain_len += i + 1, next = *desc_idx;

    // Allocate a buffer for each descriptor, using iov to manage them uniformly
    *iov = malloc(sizeof(struct iovec) * (chain_len + append_len));
    if (copy_flags)
        // Record the flag of each descriptor
        *flags = malloc(sizeof(uint16_t) * (chain_len + append_len));

    // Traverse the descriptor chain and copy the buffer pointed to by each
    // descriptor to iov
    for (i = 0; i < chain_len; i++, next = vdesc->next) {
        vdesc = &vq->desc_table[next];
        // If the descriptor supports the VRING_DESC_F_INDIRECT feature
        if (vdesc->flags & VRING_DESC_F_INDIRECT) {
            // Get the address of the indirect table pointed to by this
            // descriptor
            ind_table = (VirtqDesc *)(get_virt_addr((void *)vdesc->addr,
                                                    vq->dev->zone_id));
            table_len = vdesc->len / 16;
            log_debug("find indirect desc, table_len is %d", table_len);
            next = 0;
            for (;;) {
                // log_debug("indirect desc next is %d", next);
                ind_desc = &ind_table[next];
                descriptor2iov(i, ind_desc, *iov, *flags, vq->dev->zone_id,
                               copy_flags);
                table_len--;
                i++;
                // No more next descriptor
                if ((ind_desc->flags & VRING_DESC_F_NEXT) == 0)
                    break;
                next = ind_desc->next;
            }
            if (table_len != 0) {
                log_error("invalid indirect descriptor chain");
                break;
            }
        } else {
            // For a normal descriptor, copy it directly to iov
            descriptor2iov(i, vdesc, *iov, *flags, vq->dev->zone_id,
                           copy_flags);
        }
    }
    return chain_len;
}

void update_used_ring(VirtQueue *vq, uint16_t idx, uint32_t iolen) {
    volatile VirtqUsed *used_ring;
    volatile VirtqUsedElem *elem;
    uint16_t used_idx, mask;
    // There is no need to worry about if used_ring is full, because used_ring's
    // len is equal to descriptor table's.
    write_barrier();
    // pthread_mutex_lock(&vq->used_ring_lock);
    used_ring = vq->used_ring;
    used_idx = used_ring->idx;
    mask = vq->num - 1;
    elem = &used_ring->ring[used_idx++ & mask];
    elem->id = idx;
    elem->len = iolen;
    used_ring->idx = used_idx;
    write_barrier();
    // pthread_mutex_unlock(&vq->used_ring_lock);
    log_debug(
        "update used ring: used_idx is %d, elem->idx is %d, vq->num is %d",
        used_idx, idx, vq->num);
}

uint64_t virtio_mmio_read(VirtIODevice *vdev, uint64_t offset, unsigned size) {
    log_debug("virtio mmio read at %#x", offset);

    if (!vdev) {
        switch (offset) {
        case VIRTIO_MMIO_MAGIC_VALUE:
            log_debug("read VIRTIO_MMIO_MAGIC_VALUE");
            return VIRT_MAGIC;
        case VIRTIO_MMIO_VERSION:
            log_debug("read VIRTIO_MMIO_VERSION");
            return VIRT_VERSION;
        case VIRTIO_MMIO_VENDOR_ID:
            log_debug("read VIRTIO_MMIO_VENDOR_ID");
            return VIRT_VENDOR;
        default:
            return 0;
        }
    }

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        // the first member of vdev->dev must be config.
        log_debug("read virtio dev config");
        return *(uint64_t *)(vdev->dev + offset);
    }

    if (size != 4) {
        log_error("virtio-mmio-read: wrong size access to register!");
        return 0;
    }

    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        log_debug("read VIRTIO_MMIO_MAGIC_VALUE");
        return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
        log_debug("read VIRTIO_MMIO_VERSION");
        return VIRT_VERSION;
    case VIRTIO_MMIO_DEVICE_ID:
        log_debug("read VIRTIO_MMIO_DEVICE_ID");
        return vdev->regs.device_id;
    case VIRTIO_MMIO_VENDOR_ID:
        log_debug("read VIRTIO_MMIO_VENDOR_ID");
        return VIRT_VENDOR;
    case VIRTIO_MMIO_DEVICE_FEATURES:
        log_debug("read VIRTIO_MMIO_DEVICE_FEATURES");

        if (vdev->regs.dev_feature_sel) {
            return vdev->regs.dev_feature >> 32;
        } else {
            return vdev->regs.dev_feature;
        }
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        log_debug("read VIRTIO_MMIO_QUEUE_NUM_MAX");
        return vdev->vqs[vdev->regs.queue_sel].queue_num_max;
    case VIRTIO_MMIO_QUEUE_READY:
        log_debug("read VIRTIO_MMIO_QUEUE_READY");
        return vdev->vqs[vdev->regs.queue_sel].ready;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        log_debug("read VIRTIO_MMIO_INTERRUPT_STATUS");
        if (vdev->regs.interrupt_status == 0) {
            log_error("virtio-mmio-read: interrupt status is 0, type is %d",
                      vdev->type);
        }
        return vdev->regs.interrupt_status;
    case VIRTIO_MMIO_STATUS:
        log_debug("read VIRTIO_MMIO_STATUS");
        return vdev->regs.status;
    case VIRTIO_MMIO_CONFIG_GENERATION:
        log_debug("read VIRTIO_MMIO_CONFIG_GENERATION");
        return vdev->regs.generation;
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
    case VIRTIO_MMIO_DRIVER_FEATURES:
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
    case VIRTIO_MMIO_QUEUE_SEL:
    case VIRTIO_MMIO_QUEUE_NUM:
    case VIRTIO_MMIO_QUEUE_NOTIFY:
    case VIRTIO_MMIO_INTERRUPT_ACK:
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
    case VIRTIO_MMIO_QUEUE_USED_LOW:
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        log_error("read of write-only register");
        return 0;
    default:
        log_error("bad register offset %#x", offset);
        return 0;
    }
    return 0;
}

void virtio_mmio_write(VirtIODevice *vdev, uint64_t offset, uint64_t value,
                       unsigned size) {
    log_debug("virtio mmio write at %#x, value is %#x\n", offset, value);
    VirtMmioRegs *regs = &vdev->regs;
    VirtQueue *vqs = vdev->vqs;
    if (!vdev) {
        return;
    }

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        log_error("virtio_mmio_write: can't write config space");
        return;
    }
    if (size != 4) {
        log_error("virtio_mmio_write: wrong size access to register!");
        return;
    }

    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        log_debug("write VIRTIO_MMIO_DEVICE_FEATURES_SEL");
        if (value) {
            regs->dev_feature_sel = 1;
        } else {
            regs->dev_feature_sel = 0;
        }
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        log_debug("zone %d driver set device %s, accepted features %d",
                  vdev->zone_id, virtio_device_type_to_string(vdev->type),
                  value);
        if (regs->drv_feature_sel) {
            regs->drv_feature |= value << 32;
        } else {
            regs->drv_feature |= value;
        }

        // If the driver frontend has activated VIRTIO_RING_F_EVENT_IDX, enable
        // the related settings
        if (regs->drv_feature & (1ULL << VIRTIO_RING_F_EVENT_IDX)) {
            log_debug("zone %d driver accepted VIRTIO_RING_F_EVENT_IDX",
                      vdev->zone_id);
            int len = vdev->vqs_len;
            for (int i = 0; i < len; i++)
                vqs[i].event_idx_enabled = 1;
        }
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        log_debug("write VIRTIO_MMIO_DRIVER_FEATURES_SEL");

        if (value) {
            regs->drv_feature_sel = 1;
        } else {
            regs->drv_feature_sel = 0;
        }
        break;
    case VIRTIO_MMIO_QUEUE_SEL:
        log_debug("zone %d driver set device %s, selecting queue %d",
                  vdev->zone_id, virtio_device_type_to_string(vdev->type),
                  value);

        if (value < vdev->vqs_len) {
            regs->queue_sel = value;
        }
        break;
    case VIRTIO_MMIO_QUEUE_NUM:
        log_debug("zone %d driver set device %s, use virtqueue num %d",
                  vdev->zone_id, virtio_device_type_to_string(vdev->type),
                  value);

        vqs[regs->queue_sel].num = value;
        break;
    case VIRTIO_MMIO_QUEUE_READY:
        log_debug("write VIRTIO_MMIO_QUEUE_READY");

        vqs[regs->queue_sel].ready = value;
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        log_debug("****** zone %d %s queue notify begin ******", vdev->zone_id,
                  virtio_device_type_to_string(vdev->type));

        if (value < vdev->vqs_len) {
            log_trace("queue notify ready, handler addr is %#x",
                      vqs[value].notify_handler);
            vqs[value].notify_handler(vdev, &vqs[value]);
        }

        log_debug("****** zone %d %s queue notify end ******", vdev->zone_id,
                  virtio_device_type_to_string(vdev->type));

        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        log_debug("write VIRTIO_MMIO_INTERRUPT_ACK");

        if (value == regs->interrupt_status && regs->interrupt_count > 0) {
            regs->interrupt_count--;
            break;
        } else if (value != regs->interrupt_status) {
            log_error("interrupt_status is not equal to ack, type is %d",
                      vdev->type);
        }
        regs->interrupt_status &= !value;
        break;
    case VIRTIO_MMIO_STATUS:
        log_debug("write VIRTIO_MMIO_STATUS");

        regs->status = value;
        if (regs->status == 0) {
            virtio_dev_reset(vdev);
        }
        break;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
        log_debug("write VIRTIO_MMIO_QUEUE_DESC_LOW");

        vqs[regs->queue_sel].desc_table_addr |= value & UINT32_MAX;
        break;
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
        log_debug("write VIRTIO_MMIO_QUEUE_DESC_HIGH");

        vqs[regs->queue_sel].desc_table_addr |= value << 32;
        virtqueue_set_desc_table(&vqs[regs->queue_sel]);
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
        log_debug("write VIRTIO_MMIO_QUEUE_AVAIL_LOW");

        vqs[regs->queue_sel].avail_addr |= value & UINT32_MAX;
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
        log_debug("write VIRTIO_MMIO_QUEUE_AVAIL_HIGH");

        vqs[regs->queue_sel].avail_addr |= value << 32;
        virtqueue_set_avail(&vqs[regs->queue_sel]);
        break;
    case VIRTIO_MMIO_QUEUE_USED_LOW:
        log_debug("write VIRTIO_MMIO_QUEUE_USED_LOW");

        vqs[regs->queue_sel].used_addr |= value & UINT32_MAX;
        break;
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        log_debug("write VIRTIO_MMIO_QUEUE_USED_HIGH");

        vqs[regs->queue_sel].used_addr |= value << 32;
        virtqueue_set_used(&vqs[regs->queue_sel]);
        break;
    case VIRTIO_MMIO_MAGIC_VALUE:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICE_ID:
    case VIRTIO_MMIO_VENDOR_ID:
    case VIRTIO_MMIO_DEVICE_FEATURES:
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
    case VIRTIO_MMIO_INTERRUPT_STATUS:
    case VIRTIO_MMIO_CONFIG_GENERATION:
        log_error("%s: write to read-only register 0#x", __func__, offset);
        break;

    default:
        log_error("%s: bad register offset 0#x", __func__, offset);
    }
}

inline bool in_range(uint64_t value, uint64_t lower, uint64_t len) {
    return ((value >= lower) && (value < (lower + len)));
}

// Inject irq_id to target zone. It will add to res list, and notify hypervisor
// through ioctl.
void virtio_inject_irq(VirtQueue *vq) {
    uint16_t last_used_idx, idx, event_idx;
    last_used_idx = vq->last_used_idx;
    vq->last_used_idx = idx = vq->used_ring->idx;
    // read_barrier();
    if (idx == last_used_idx) {
        log_debug("idx equals last_used_idx");
        return;
    }
    if (!vq->event_idx_enabled &&
        (vq->avail_ring->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
        log_debug("no interrupt");
        return;
    }
    if (vq->event_idx_enabled) {
        event_idx = VQ_USED_EVENT(vq);
        log_debug("idx is %d, event_idx is %d, last_used_idx is %d", idx,
                  event_idx, last_used_idx);
        if (!vring_need_event(event_idx, idx, last_used_idx)) {
            return;
        }
    }
    volatile struct device_res *res;
    while (is_queue_full(virtio_bridge->res_front, virtio_bridge->res_rear,
                         MAX_REQ))
        ;
    pthread_mutex_lock(&RES_MUTEX);
    unsigned int res_rear = virtio_bridge->res_rear;
    res = &virtio_bridge->res_list[res_rear];
    res->irq_id = vq->dev->irq_id;
    res->target_zone = vq->dev->zone_id;
    write_barrier();
    virtio_bridge->res_rear = (res_rear + 1) & (MAX_REQ - 1);
    write_barrier();
    vq->dev->regs.interrupt_status = VIRTIO_MMIO_INT_VRING;
    vq->dev->regs.interrupt_count++;
    pthread_mutex_unlock(&RES_MUTEX);
    log_debug("inject irq to device %s, vq is %d",
              virtio_device_type_to_string(vq->dev->type), vq->vq_idx);
    ioctl(ko_fd, HVISOR_FINISH_REQ);
}

void virtio_finish_cfg_req(uint32_t target_cpu, uint64_t value) {
    virtio_bridge->cfg_values[target_cpu] = value;
    write_barrier();
    virtio_bridge->cfg_flags[target_cpu]++;
    write_barrier();
}

int virtio_handle_req(volatile struct device_req *req) {
    int i;
    uint64_t value = 0;

    // Check if the request corresponds to a virtio device in a specific zone
    for (i = 0; i < vdevs_num; ++i) {
        if ((req->src_zone == vdevs[i]->zone_id) &&
            in_range(req->address, vdevs[i]->base_addr,
                     vdevs[i]->len)) // Check if memory regions overlap
            break;
    }

    if (i == vdevs_num) {
        log_warn("no matched virtio dev in zone %d, address is 0x%x",
                 req->src_zone, req->address);
        value = virtio_mmio_read(NULL, 0, 0);
        virtio_finish_cfg_req(req->src_cpu, value);
        return -1;
    }

    VirtIODevice *vdev = vdevs[i];

    // if (vdev->type == VirtioTNet) {
    //   log_info("handling request to net from zone %d", vdev->zone_id);
    // } else if (vdev->type == VirtioTBlock) {
    //   log_info("handling request to blk from zone %d", vdev->zone_id);
    // } else if (vdev->type == VirtioTConsole) {
    //   log_info("handling request to console from zone %d", vdev->zone_id);
    // } else if (vdev->type == VirtioTGPU) {
    //   log_info("handling request to gpu from zone %d", vdev->zone_id);
    // }

    uint64_t offs = req->address - vdev->base_addr;

    // Write or read the device's MMIO register
    if (req->is_write) {
        virtio_mmio_write(vdev, offs, req->value, req->size);
    } else {
        value = virtio_mmio_read(vdev, offs, req->size);
        log_debug("read value is 0x%x\n", value);
    }

    // Control instructions do not require interrupts to return data
    // The requester will block and wait
    if (!req->need_interrupt) {
        // If a request is a control not a data request
        virtio_finish_cfg_req(req->src_cpu, value);
    }

    log_trace("src_zone is %d, src_cpu is %lld", req->src_zone, req->src_cpu);
    return 0;
}

void virtio_close() {
    log_warn("virtio devices will be closed");
    destroy_event_monitor();
    for (int i = 0; i < vdevs_num; i++)
        vdevs[i]->virtio_close(vdevs[i]);
    close(ko_fd);
    munmap((void *)virtio_bridge, MMAP_SIZE);
    for (int i = 0; i < MAX_ZONES; i++) {
        for (int j = 0; j < MAX_RAMS; j++)
            if (zone_mem[i][j][MEM_SIZE] != 0) {
                munmap((void *)zone_mem[i][j][VIRT_ADDR],
                       zone_mem[i][j][MEM_SIZE]);
            }
    }
    mutithread_log_exit();
    log_warn("virtio daemon exit successfully");
}

void handle_virtio_requests() {
    int sig;
    sigset_t wait_set;
    struct timespec timeout;
    unsigned int req_front = virtio_bridge->req_front;
    volatile struct device_req *req;
    timeout.tv_sec = 0;
    timeout.tv_nsec = WAIT_TIME;
    sigemptyset(&wait_set);
    sigaddset(&wait_set, SIGHVI);
    sigaddset(&wait_set, SIGTERM);
    virtio_bridge->need_wakeup = 1;

    int signal_count = 0, proc_count = 0;
    unsigned long long count = 0;
    for (;;) {
        log_warn("signal_count is %d, proc_count is %d", signal_count,
                 proc_count);
        sigwait(&wait_set, &sig);
        signal_count++;
        if (sig == SIGTERM) {
            virtio_close();
            break;
        } else if (sig != SIGHVI) {
            log_error("unknown signal %d", sig);
            continue;
        }
        while (1) {
            if (!is_queue_empty(req_front, virtio_bridge->req_rear)) {
                count = 0;
                proc_count++;
                req = &virtio_bridge->req_list[req_front];
                virtio_bridge->need_wakeup = 0;
                virtio_handle_req(req);
                req_front = (req_front + 1) & (MAX_REQ - 1);
                virtio_bridge->req_front = req_front;
                write_barrier();
            } else {
                count++;
                if (count < 10000000)
                    continue;
                count = 0;
                virtio_bridge->need_wakeup = 1;
                write_barrier();
                nanosleep(&timeout, NULL);
                if (is_queue_empty(req_front, virtio_bridge->req_rear)) {
                    break;
                }
            }
        }
    }
}

void initialize_log() {
    int log_level;
#ifdef HLOG
    log_level = HLOG;
#else
    log_level = LOG_WARN;
#endif
    log_set_level(log_level);

    FILE *log_file = fopen("log.txt", "w+");
    log_add_fp(log_file, LOG_WARN);
}

int virtio_init() {
    // The higher log level is, the faster virtio-blk will be.
    int err;

    // Define signal set and add all signals to the set
    sigset_t block_mask;
    sigfillset(&block_mask);
    pthread_sigmask(SIG_BLOCK, &block_mask, NULL);

    // Set process name
    prctl(PR_SET_NAME, "hvisor-virtio", 0, 0, 0);

    // Initialize logging
    multithread_log_init();
    initialize_log();

    log_info("hvisor init");
    ko_fd = open("/dev/hvisor", O_RDWR);
    if (ko_fd < 0) {
        log_error("open hvisor failed");
        exit(1);
    }
    // ioctl for init virtio
    // Communicate with hvisor kernel module
    err = ioctl(ko_fd, HVISOR_INIT_VIRTIO);
    if (err) {
        log_error("ioctl failed, err code is %d", err);
        close(ko_fd);
        exit(1);
    }

    // mmap: create shared memory
    // Map the virtio_bridge set by the kernel module to this space
    virtio_bridge = (struct virtio_bridge *)mmap(
        NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ko_fd, 0);
    if (virtio_bridge == (void *)-1) {
        log_error("mmap failed");
        goto unmap;
    }

    // Initialize event_monitor used by console and net devices
    initialize_event_monitor();
    log_info("hvisor init okay!");
    return 0;
unmap:
    munmap((void *)virtio_bridge, MMAP_SIZE);
    return -1;
}

int create_virtio_device_from_json(cJSON *device_json, int zone_id) {
    VirtioDeviceType dev_type = VirtioTNone;
    uint64_t base_addr = 0, len = 0;
    uint32_t irq_id = 0;

    GPURequestedState *requested_state = NULL;

    char *status = cJSON_GetObjectItem(device_json, "status")->valuestring;
    if (strcmp(status, "disable") == 0)
        return 0;

    // Get device type
    char *type = cJSON_GetObjectItem(device_json, "type")->valuestring;
    void *arg0, *arg1;

    // Match the device type field in json
    if (strcmp(type, "blk") == 0) {
        dev_type = VirtioTBlock;
    } else if (strcmp(type, "net") == 0) {
        dev_type = VirtioTNet;
    } else if (strcmp(type, "console") == 0) {
        dev_type = VirtioTConsole;
    } else if (strcmp(type, "gpu") == 0) {
        dev_type = VirtioTGPU;
    } else {
        log_error("unknown device type %s", type);
        return -1;
    }

    // Get base_addr, len, irq_id (mmio region base address and length, device
    // interrupt number)
    base_addr = strtoul(cJSON_GetObjectItem(device_json, "addr")->valuestring,
                        NULL, 16);
    len =
        strtoul(cJSON_GetObjectItem(device_json, "len")->valuestring, NULL, 16);
    irq_id = cJSON_GetObjectItem(device_json, "irq")->valueint;

    // Handle other fields according to the device type
    if (dev_type == VirtioTBlock) {
        // virtio-blk
        char *img = cJSON_GetObjectItem(device_json, "img")->valuestring;
        arg0 = img, arg1 = NULL;
    } else if (dev_type == VirtioTNet) {
        // virtio-net
        char *tap = cJSON_GetObjectItem(device_json, "tap")->valuestring;
        cJSON *mac_json = cJSON_GetObjectItem(device_json, "mac");
        uint8_t mac[6];
        for (int i = 0; i < 6; i++) {
            mac[i] =
                strtoul(cJSON_GetArrayItem(mac_json, i)->valuestring, NULL, 16);
        }
        arg0 = mac, arg1 = tap;
    } else if (dev_type == VirtioTConsole) {
        // virtio-console
        arg0 = arg1 = NULL;
    } else if (dev_type == VirtioTGPU) {
        // virtio-gpu
        // TODO: Add display device settings
        requested_state =
            (GPURequestedState *)malloc(sizeof(GPURequestedState));
        memset(requested_state, 0, sizeof(GPURequestedState));
        requested_state->width =
            cJSON_GetObjectItem(device_json, "width")->valueint;
        requested_state->height =
            cJSON_GetObjectItem(device_json, "height")->valueint;
        arg0 = requested_state;
        arg1 = NULL;
    }

    // Check for missing fields
    if (base_addr == 0 || len == 0 || irq_id == 0) {
        log_error("missing arguments");
        return -1;
    }

    // Create virtio_device
    if (!create_virtio_device(dev_type, zone_id, base_addr, len, irq_id, arg0,
                              arg1)) {
        return -1;
    }

    return 0;
}

int virtio_start_from_json(char *json_path) {
    char *buffer = NULL;
    u_int64_t file_size;
    int zone_id, num_devices = 0, err = 0, num_zones = 0;
    void *zone0_ipa, *zonex_ipa, *virt_addr;
    unsigned long long mem_size;
    buffer = read_file(json_path, &file_size);
    buffer[file_size] = '\0';

    // Read zones
    cJSON *root = cJSON_Parse(buffer);
    cJSON *zones_json = cJSON_GetObjectItem(root, "zones");
    num_zones = cJSON_GetArraySize(zones_json);
    if (num_zones > MAX_ZONES) {
        log_error("Exceed maximum zone number");
        err = -1;
        goto err_out;
    }

    // Match zone information
    for (int i = 0; i < num_zones; i++) {
        cJSON *zone_json = cJSON_GetArrayItem(zones_json, i);
        cJSON *zone_id_json = cJSON_GetObjectItem(zone_json, "id");
        cJSON *memory_region_json =
            cJSON_GetObjectItem(zone_json, "memory_region");
        cJSON *devices_json = cJSON_GetObjectItem(zone_json, "devices");
        zone_id = zone_id_json->valueint;
        if (zone_id >= MAX_ZONES) {
            log_error("Exceed maximum zone number");
            err = -1;
            goto err_out;
        }
        int num_mems = cJSON_GetArraySize(memory_region_json);

        // Memory regions
        for (int j = 0; j < num_mems; j++) {
            cJSON *mem_region = cJSON_GetArrayItem(memory_region_json, j);
            zone0_ipa = strtoull(
                cJSON_GetObjectItem(mem_region, "zone0_ipa")->valuestring, NULL,
                16);
            zonex_ipa = strtoull(
                cJSON_GetObjectItem(mem_region, "zonex_ipa")->valuestring, NULL,
                16);
            mem_size = strtoull(
                cJSON_GetObjectItem(mem_region, "size")->valuestring, NULL, 16);
            if (mem_size == 0) {
                log_error("Invalid memory size");
                continue;
            }
            // Map from zone0_ipa
            virt_addr = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             ko_fd, (off_t)zone0_ipa);
            if (virt_addr == (void *)-1) {
                log_error("mmap failed");
                err = -1;
                goto err_out;
            }
            zone_mem[zone_id][j][VIRT_ADDR] = virt_addr;
            zone_mem[zone_id][j][ZONE0_IPA] = zone0_ipa;
            zone_mem[zone_id][j][ZONEX_IPA] = zonex_ipa;
            zone_mem[zone_id][j][MEM_SIZE] = mem_size;
        }

        num_devices = cJSON_GetArraySize(devices_json);
        for (int j = 0; j < num_devices; j++) {
            cJSON *device = cJSON_GetArrayItem(devices_json, j);
            err = create_virtio_device_from_json(device, zone_id);
            if (err) {
                log_error("create virtio device failed");
                goto err_out;
            }
        }
    }

err_out:
    cJSON_Delete(root);
    free(buffer);
    return err;
}

int virtio_start(int argc, char *argv[]) {
    int opt, err = 0;
    err = virtio_init(); // Initialize virtio dependencies
    if (err)
        return -1;

    err = virtio_start_from_json(
        argv[3]); // Start virtio devices based on virtio_cfg_*.json
    if (err)
        goto err_out;

    for (int i = 0; i < vdevs_num; i++) {
        virtio_bridge->mmio_addrs[i] = vdevs[i]->base_addr;
    }

    write_barrier();
    virtio_bridge->mmio_avail = 1;
    write_barrier();

    handle_virtio_requests(); // Handle virtio requests
    return 0;
err_out:
    virtio_close();
    return err;
}
