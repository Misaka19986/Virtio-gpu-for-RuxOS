#ifndef _HVISOR_VIRTIO_GPU_H
#define _HVISOR_VIRTIO_GPU_H
#include "linux/types.h"
#include "sys/queue.h"
#include "virtio.h"
#include <linux/virtio_gpu.h>
#include <stddef.h>
#include <stdint.h>

/*********************************************************************
    宏定义
 */
// controlq的idx
#define GPU_CONTROL_QUEUE 0

// cursorq的idx
#define GPU_CURSOR_QUEUE 1

// virtqueue数量
#define GPU_MAX_QUEUES 2

// virtqueue中元素的最大数量
#define VIRTQUEUE_GPU_MAX_SIZE 256

// hvisor的virtio_gpu实现所支持的最大scanouts数量
#define HVISOR_VIRTIO_GPU_MAX_SCANOUTS 1

// 一个virtio gpu设备所能占用的最大用于存储resource的内存
#define VIRTIO_GPU_MAX_HOSTMEM 536870912 // 512MB

// 支持的virtio features
// 可选VIRTIO_RING_F_INDIRECT_DESC和VIRTIO_RING_F_EVENT_IDX
// 待支持VIRTIO_GPU_F_EDID、VIRTIO_GPU_F_RESOURCE_UUID、VIRTIO_GPU_F_RESOURCE_BLOB、VIRTIO_GPU_F_VIRGL、VIRTIO_GPU_F_CONTEXT_INIT
#define GPU_SUPPORTED_FEATURES ((1ULL << VIRTIO_F_VERSION_1))

// 求最小值宏
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/*********************************************************************
    结构体
 */
// virtio_gpu_config如下
// #define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)
// struct virtio_gpu_config {
// 	__u32 events_read;  // 等待驱动读取的事件(VIRTIO_GPU_EVENT_DISPLAY)
// 	__u32 events_clear; // 清除驱动中的等待事件
// 	__u32 num_scanouts; //
// 最大支持的scanouts数量，见相关宏定义VIRTIO_GPU_MAX_SCANOUTS
// 	__u32 num_capsets;  // 设备的扩展能力集，见相关宏定义VIRTIO_GPU_CAPSET_*
// };

// 清除events
// events_read &= ~events_clear

typedef struct virtio_gpu_config GPUConfig;
typedef struct virtio_gpu_ctrl_hdr GPUControlHeader;
typedef struct virtio_gpu_update_cursor GPUUpdateCursor;

// host内存中的image，用于输出到显示设备
typedef struct virtio_gpu_simple_image {
  uint32_t format;
  uint32_t width, height;
  uint32_t stride;
  uint32_t *data;
} GPUSimpleImage;

// 渲染时存储在内存中的资源对象(图片等)
typedef struct virtio_gpu_simple_resource {
  uint32_t resource_id;   // 资源id
  uint32_t width, height; // 资源的宽和高
  uint32_t format; // 资源的format，结合宽高计算存储资源所用的内存大小
  // addrs为资源在guest的物理地址数组，因为zonex会直接映射到hvisor-tool的虚拟内存空间，因此暂时不需要该字段
  // uint64_t *addrs;
  struct iovec *iov; // 用iov来存储资源
  unsigned int iov_cnt;
  uint64_t hostmem;         // 资源在host中所占的大小
  uint32_t scanout_bitmask; // 标记resource被哪个scanout使用
  GPUSimpleImage *image;
  TAILQ_ENTRY(virtio_gpu_simple_resource) next;
} GPUSimpleResource;

typedef struct virtio_gpu_framebuffer {
  // TODO: format格式
  // format主要决定了每一个像素占多少字节
  // virtio_gpu_formats提供的都是4bytes per pixel大小的格式
  uint32_t format;        // virtio_gpu_formats
  uint32_t bytes_pp;      // 每像素(per pixel)的字节大小
  uint32_t width, height; // 帧缓存的宽和高
  uint32_t stride; // stride(步幅)指图像的每一行在内存中所占的字节数，stride *
                   // height等于总字节数(hostmem)
  uint32_t offset;
} GPUFrameBuffer;

// 32-bit RGBA
typedef struct hvisor_cursor {
  uint16_t width, height;
  int hot_x, hot_y;
  int refcount;
  uint32_t data[];
} HvCursor;

// 实际显示设备相关的设置
typedef struct virtio_gpu_scanout {
  uint32_t width, height;
  int x, y;
  uint32_t resource_id;
  GPUUpdateCursor cursor;
  HvCursor *current_cursor;
  GPUFrameBuffer frame_buffer;
} GPUScanout;

// 由json指定的显示设备的设置
typedef struct virtio_gpu_requested_state {
  uint32_t width, height;
  int x, y;
} GPURequestedState;

// GPU设备结构体
typedef struct virtio_gpu_dev {
  GPUConfig config;
  // TODO: scanouts初始化
  // virtio gpu拥有的scanouts(预分配)
  GPUScanout scanouts[HVISOR_VIRTIO_GPU_MAX_SCANOUTS];
  // TODO: requested_state初始化
  // 这些scanouts协商出的requested_state
  GPURequestedState requested_states[HVISOR_VIRTIO_GPU_MAX_SCANOUTS];
  // virtio gpu拥有的resource
  TAILQ_HEAD(, virtio_gpu_simple_resource) resource_list;
  // virtio gpu需要异步处理的命令队列
  TAILQ_HEAD(, virtio_gpu_control_cmd) command_queue;
  // scanout的具体数目
  int scanouts_num;
  // virtio设备所占有的总内存
  uint64_t hostmem;
  // 启用的scanout
  int enabled_scanout_bitmask;
} GPUDev;

typedef struct virtio_gpu_control_cmd {
  GPUControlHeader control_header;
  struct iovec *resp_iov;    // 响应要写的iov
  unsigned int resp_iov_cnt; // iov个数
  uint16_t resp_idx;         // 请求完成后，其resq对应的used idex
  bool
      finished; // 表示当前cmd经处理后是否完成响应，如果没有则统一使用no_data响应
  uint32_t error;                           // 报错类型
  TAILQ_ENTRY(virtio_gpu_control_cmd) next; // TODO: 异步请求处理
} GPUCommand;

/*********************************************************************
  virtio_gpu_base.c
 */
// 初始化GPUDev结构体
GPUDev *init_gpu_dev(GPURequestedState *requested_states);

// 初始化virtio-gpu设备
int virtio_gpu_init(VirtIODevice *vdev);

// 关闭virtio-gpu设备
void virtio_gpu_close(VirtIODevice *vdev);

// 重置virtio-gpu设备
void virtio_gpu_reset();

// controlq有可处理请求时的处理函数
int virtio_gpu_ctrl_notify_handler(VirtIODevice *vdev, VirtQueue *vq);

// cursorq有可处理请求时的处理函数
int virtio_gpu_cursor_notify_handler(VirtIODevice *vdev, VirtQueue *vq);

// 处理单个请求
int virtio_gpu_handle_single_request(VirtIODevice *vdev, VirtQueue *vq);

// 从iov拷贝请求控制结构体
#define VIRTIO_GPU_FILL_CMD(iov, iov_cnt, out)                                 \
  do {                                                                         \
    size_t virtiogpufillcmd_s_ =                                               \
        iov_to_buf(iov, iov_cnt, 0, &out, sizeof(out));                        \
    if (virtiogpufillcmd_s_ != sizeof(out)) {                                  \
      log_error("cannot fill virtio gpu command with input!");                 \
      return;                                                                  \
    }                                                                          \
  } while (0)

// 从iov拷贝任意大小到buffer
size_t iov_to_buf_full(const struct iovec *iov, const unsigned int iov_cnt,
                       size_t offset, void *buf, size_t bytes_need_copy);

// 从buffer拷贝任意大小到iov
size_t buf_to_iov_full(const struct iovec *iov, unsigned int iov_cnt,
                       size_t offset, const void *buf, size_t bytes_need_copy);

// 从iov填充buffer，单次优化
// 单纯使用command结构体中某一字段，或者vq长度仅为2的情况较多
// 因此需要在只对iov中的一个buffer进行拷贝的情况下的优化
static inline size_t iov_to_buf(const struct iovec *iov, // NOLINT
                                const unsigned int iov_cnt, size_t offset,
                                void *buf, size_t bytes_need_copy) {
  if (__builtin_constant_p(bytes_need_copy) && iov_cnt &&
      offset <= iov[0].iov_len && bytes_need_copy <= iov[0].iov_len - offset) {
    // 如果只需要在iov的第一个buffer就可以完成copy操作
    // 拷贝立刻返回
    memcpy(buf, (char *)iov[0].iov_base + offset, bytes_need_copy);
    return bytes_need_copy;
  } else {
    // 需要从iov的多个buffer进行拷贝
    return iov_to_buf_full(iov, iov_cnt, offset, buf, bytes_need_copy);
  }
}

// 从buffer填充iov，单次优化
static inline size_t buf_to_iov(const struct iovec *iov, // NOLINT
                                unsigned int iov_cnt, size_t offset,
                                const void *buf, size_t bytes_need_copy) {
  if (__builtin_constant_p(bytes_need_copy) && iov_cnt &&
      offset <= iov[0].iov_len && bytes_need_copy <= iov[0].iov_len - offset) {
    memcpy((char *)iov[0].iov_base + offset, buf, bytes_need_copy);
    return bytes_need_copy;
  } else {
    return buf_to_iov_full(iov, iov_cnt, offset, buf, bytes_need_copy);
  }
}

/*********************************************************************
  virtio_gpu.c
 */
// 返回有数据的响应
static void virtio_gpu_ctrl_response(VirtIODevice *vdev, GPUCommand *gcmd,
                                     GPUControlHeader *resp, size_t resp_len);

// 返回无数据的响应
static void virtio_gpu_ctrl_response_nodata(VirtIODevice *vdev,
                                            GPUCommand *gcmd,
                                            enum virtio_gpu_ctrl_type type);

// 对应VIRTIO_GPU_CMD_GET_DISPLAY_INFO
// 返回当前的输出配置
static void virtio_gpu_get_display_info(VirtIODevice *vdev, GPUCommand *gcmd);

// 对应VIRTIO_GPU_CMD_GET_EDID
// 返回当前的EDID信息
static void virtio_gpu_get_edid(VirtIODevice *vdev, GPUCommand *gcmd);

// 对应VIRTIO_GPU_CMD_RESOURCE_CREATE_2D
// 在host上以guest给定的id，width，height，format创建一个2D资源
static void virtio_gpu_resource_create_2d(VirtIODevice *vdev, GPUCommand *gcmd);

// 查询给定的virtio gpu设备是否有id为resource_id的资源
// 若没有，则返回NULL
static GPUSimpleResource *virtio_gpu_find_resource(GPUDev *gdev,
                                                   uint32_t resource_id);

// 检查指定id的resource是否已经绑定，如果是，则返回其指针
// 否则，若id没有对应的resource，或者该resource没有绑定，则返回NULL
static GPUSimpleResource *virtio_gpu_check_resource(VirtIODevice *vdev,
                                                    uint32_t resource_id,
                                                    const char *caller,
                                                    uint32_t *error);

// 计算resource在host所占用的内存大小
static uint32_t calc_image_hostmem(int bits_per_pixel, uint32_t width,
                                   uint32_t height);

// 对应VIRTIO_GPU_CMD_RESOURCE_UNREF
// 销毁一个resource
static void virtio_gpu_resource_unref(VirtIODevice *vdev, GPUCommand *gcmd);

// 对应VIRTIO_GPU_CMD_RESOURCE_FLUSH
// flush一个已经链接到scanout的resource
static void virtio_gpu_resource_flush(VirtIODevice *vdev, GPUCommand *gcmd);

// 对应VIRTIO_GPU_CMD_SET_SCANOUT
// 设置scanout的display参数，为scanout绑定resource
static void virtio_gpu_set_scanout(VirtIODevice *vdev, GPUCommand *gcmd);

// 根据参数具体设置scanout
static bool virtio_gpu_do_set_scanout(VirtIODevice *vdev, uint32_t scanout_id,
                                      GPUFrameBuffer *fb,
                                      GPUSimpleResource *res,
                                      struct virtio_gpu_rect *r,
                                      uint32_t *error);

// 更新scanout
static void virtio_gpu_update_scanout(VirtIODevice *vdev, uint32_t scanout_id,
                                      GPUFrameBuffer *fb,
                                      GPUSimpleResource *res,
                                      struct virtio_gpu_rect *r);

// 对应VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D
// 将在guest内存中的内容转移到host的resource中
static void virtio_gpu_transfer_to_host_2d(VirtIODevice *vdev,
                                           GPUCommand *gcmd);

// 对应VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING
// 将多个guest中的内存区域绑定到resource(作为backing storage)
static void virtio_gpu_resource_attach_backing(VirtIODevice *vdev,
                                               GPUCommand *gcmd);

// 将guest内存映射到host的iov
static int virtio_gpu_create_mapping_iov(VirtIODevice *vdev,
                                         uint32_t nr_entries, uint32_t offset,
                                         GPUCommand *gcmd, /*uint64_t **addr,*/
                                         struct iovec **iov, uint32_t *niov);

// ! reserved
// 清除映射
// static void virtio_gpu_cleanup_mapping_iov(VirtIODevice *vdev,
//                                            struct iovec *iov, uint32_t
//                                            iov_cnt);

// 对应VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING
// 从resource中解绑guest的内存区域
static void virtio_gpu_resource_detach_backing(VirtIODevice *vdev,
                                               GPUCommand *gcmd);

// 根据control header处理请求
static void virtio_gpu_simple_process_cmd(struct iovec *iov,
                                          const unsigned int iov_cnt,
                                          uint16_t resp_idx,
                                          VirtIODevice *vdev);

#endif /* _HVISOR_VIRTIO_GPU_H */