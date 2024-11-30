#include "virtio_gpu.h"
#include "linux/virtio_gpu.h"
#include "log.h"
#include "sys/queue.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void virtio_gpu_ctrl_response(VirtIODevice *vdev, GPUCommand *gcmd,
                                     GPUControlHeader *resp, size_t resp_len) {
  log_debug("sending response");
  // 因为每个response结构体的头部都是GPUControlHeader，因此其地址就是response结构体的地址

  // iov[0]所对应的第一个描述符一定是只读的，因此从第二个开始
  size_t s =
      buf_to_iov(&gcmd->resp_iov[1], gcmd->resp_iov_cnt - 1, 0, resp, resp_len);

  if (s != resp_len) {
    log_error("%s cannot copy buffer to iov with correct size", __func__);
    // 继续返回，交由前端处理
  }

  // 根据命令选择返回vq
  int sel = GPU_CONTROL_QUEUE;

  if (gcmd->control_header.type == VIRTIO_GPU_CMD_UPDATE_CURSOR ||
      gcmd->control_header.type == VIRTIO_GPU_CMD_MOVE_CURSOR) {
    sel = GPU_CURSOR_QUEUE;
  }

  update_used_ring(&vdev->vqs[sel], gcmd->resp_idx, resp_len);

  gcmd->finished = true;
}

static void virtio_gpu_ctrl_response_nodata(VirtIODevice *vdev,
                                            GPUCommand *gcmd,
                                            enum virtio_gpu_ctrl_type type) {
  log_debug("entering %s", __func__);

  GPUControlHeader resp;

  memset(&resp, 0, sizeof(resp));
  resp.type = type;
  virtio_gpu_ctrl_response(vdev, gcmd, &resp, sizeof(resp));
}

static void virtio_gpu_get_display_info(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);

  struct virtio_gpu_resp_display_info display_info;
  GPUDev *gdev = vdev->dev;

  memset(&display_info, 0, sizeof(display_info));
  display_info.hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;

  // 向响应结构体填入display信息
  for (int i = 0; i < HVISOR_VIRTIO_GPU_MAX_SCANOUTS; ++i) {
    display_info.pmodes[i].enabled = 1;
    display_info.pmodes[i].r.width = gdev->requested_states[i].width;
    display_info.pmodes[i].r.height = gdev->requested_states[i].height;
    log_debug("return display info of scanout %d with width %d, height %d", i,
              display_info.pmodes[i].r.width, display_info.pmodes[i].r.height);
  }

  virtio_gpu_ctrl_response(vdev, gcmd, &display_info.hdr, sizeof(display_info));
}

static void virtio_gpu_get_edid(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
  // TODO
}

static void virtio_gpu_resource_create_2d(VirtIODevice *vdev,
                                          GPUCommand *gcmd) {
  log_debug("entering %s", __func__);

  GPUSimpleResource *res;
  GPUDev *gdev = vdev->dev;
  struct virtio_gpu_resource_create_2d create_2d;

  VIRTIO_GPU_FILL_CMD(gcmd->resp_iov, gcmd->resp_iov_cnt, create_2d);

  // 检查想要创建的resource_id是否是0(无法使用0)
  if (create_2d.resource_id == 0) {
    log_error("%s trying to create 2d resource with id 0", __func__);
    gcmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }

  // 检查资源是否已经创建
  res = virtio_gpu_find_resource(gdev, create_2d.resource_id);
  if (res) {
    log_error("%s trying to create an existing resource with id %d", __func__,
              create_2d.resource_id);
    gcmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }

  // 否则新建一个resource
  res = calloc(1, sizeof(GPUSimpleResource));
  memset(res, 0, sizeof(GPUSimpleResource));

  res->width = create_2d.width;
  res->height = create_2d.height;
  res->format = create_2d.format;
  res->resource_id = create_2d.resource_id;
  res->iov = NULL;
  res->iov_cnt = 0;

  // 计算resource所占用的内存大小
  // 默认只支持bpp为4 bytes大小的format
  res->hostmem = calc_image_hostmem(32, create_2d.width, create_2d.height);
  if (res->hostmem + gdev->hostmem < VIRTIO_GPU_MAX_HOSTMEM) {
    // 内存足够，将res加入virtio gpu下管理
    TAILQ_INSERT_HEAD(&gdev->resource_list, res, next);
    gdev->hostmem += res->hostmem;

    log_debug("add a resource %d to gpu dev of zone %d, width: %d height: %d "
              "format: %d mem: %d host-hostmem: %d",
              res->resource_id, vdev->zone_id, res->width, res->height,
              res->format, res->hostmem, gdev->hostmem);

    return;
  } else {
    log_error("virtio gpu for zone %d out of hostmem when trying to create "
              "resource %d",
              vdev->zone_id, create_2d.resource_id);
    free(res);
    return;
  }
}

static GPUSimpleResource *virtio_gpu_find_resource(GPUDev *gdev,
                                                   uint32_t resource_id) {
  GPUSimpleResource *temp_res;
  TAILQ_FOREACH(temp_res, &gdev->resource_list, next) {
    if (temp_res->resource_id == resource_id) {
      return temp_res;
    }
  }
  return NULL;
}

static uint32_t calc_image_hostmem(int bits_per_pixel, uint32_t width,
                                   uint32_t height) {
  // 0x1f = 31, >> 5等价于除32，即将每行的bits数对齐到4 bytes的倍数
  // 最后乘sizeof(uint32)获得字节数
  int stride = ((width * bits_per_pixel + 0x1f) >> 5) * sizeof(uint32_t);
  // 返回所占内存的总大小
  return height * stride;
}

static void virtio_gpu_resource_unref(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

static void virtio_gpu_resource_flush(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

static void virtio_gpu_set_scanout(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

static void virtio_gpu_transfer_to_host_2d(VirtIODevice *vdev,
                                           GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

static void virtio_gpu_resource_attach_backing(VirtIODevice *vdev,
                                               GPUCommand *gcmd) {
  log_debug("entering %s", __func__);

  GPUSimpleResource *res;
  struct virtio_gpu_resource_attach_backing attach_backing;
  GPUDev *gdev = vdev->dev;

  VIRTIO_GPU_FILL_CMD(gcmd->resp_iov, gcmd->resp_iov_cnt, attach_backing);

  // 检查需要绑定的resource是否已经注册
  res = virtio_gpu_find_resource(gdev, attach_backing.resource_id);
  if (!res) {
    log_error("%s cannot find resource with id %d", __func__,
              attach_backing.resource_id);
    gcmd->error = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }

  if (res->iov) {
    log_error("%s found resource %d already has iov", __func__,
              attach_backing.resource_id);
    gcmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
    return;
  }

  log_debug("attaching guest mem to resource %d of gpu dev from zone %d",
            res->resource_id, vdev->zone_id);

  int err = virtio_gpu_create_mapping_iov(
      vdev, attach_backing.nr_entries, sizeof(attach_backing), gcmd,
      &res->addrs, &res->iov, &res->iov_cnt);
  if (err != 0) {
    log_error("%s failed to map guest memory to iov", __func__);
    gcmd->error = VIRTIO_GPU_RESP_ERR_UNSPEC;
    return;
  }
}

static int virtio_gpu_create_mapping_iov(VirtIODevice *vdev,
                                         uint32_t nr_entries, uint32_t offset,
                                         GPUCommand *gcmd, uint64_t **addr,
                                         struct iovec **iov, uint32_t *niov) {
  GPUDev *gdev = vdev->dev;

  struct virtio_gpu_mem_entry *entries; // 存储所有guest内存入口的数组
  size_t entries_size;

  if (nr_entries > 16384) {
    log_error(
        "%s find number of entries %d is too big (need to less than 16384)",
        __func__, nr_entries);
    return -1;
  }

  // 分配内存，并且将guest mem用iov管理
  // guest内存到host内存需要一层转换
  entries_size = sizeof(*entries) * nr_entries;
  entries = malloc(entries_size);
  log_debug("%s got %d entries with total size %d", __func__, nr_entries,
            entries_size);
  // 先将请求中附带的所有内存入口拷贝到iov
  size_t s = iov_to_buf(gcmd->resp_iov, gcmd->resp_iov_cnt, offset, entries,
                        entries_size);

  return 0;
}

static void virtio_gpu_resource_detach_backing(VirtIODevice *vdev,
                                               GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

static void virtio_gpu_simple_process_cmd(struct iovec *iov,
                                          const unsigned int iov_cnt,
                                          uint16_t resp_idx,
                                          VirtIODevice *vdev) {
  log_debug("------ entering %s ------", __func__);

  GPUCommand gcmd;
  gcmd.resp_iov = iov;
  gcmd.resp_iov_cnt = iov_cnt;
  gcmd.resp_idx = resp_idx;
  gcmd.error = 0;
  gcmd.finished = false;

  // 先填充每个请求都有的cmd_hdr
  VIRTIO_GPU_FILL_CMD(iov, iov_cnt, gcmd.control_header);

  // 根据cmd_hdr的类型跳转到对应的处理函数
  /**********************************
   * 一般的2D渲染调用链是get_display_info->resource_create_2d->resource_attach_backing->set_scanout->get_display_info
   * ->transfer_to_host_2d->resource_flush->*重复transfer和flush*->结束
   */
  switch (gcmd.control_header.type) {
  case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
    virtio_gpu_get_display_info(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_GET_EDID:
    virtio_gpu_get_edid(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
    virtio_gpu_resource_create_2d(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_RESOURCE_UNREF:
    virtio_gpu_resource_unref(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
    virtio_gpu_resource_flush(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
    virtio_gpu_transfer_to_host_2d(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_SET_SCANOUT:
    virtio_gpu_set_scanout(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
    virtio_gpu_resource_attach_backing(vdev, &gcmd);
    break;
  case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
    virtio_gpu_resource_detach_backing(vdev, &gcmd);
    break;
  default:
    log_error("unknown request type");
    gcmd.error = VIRTIO_GPU_RESP_ERR_UNSPEC;
    break;
  }

  if (!gcmd.finished) {
    // 如果没有直接返回有data的响应，那么检查是否产生错误并返回无data响应
    if (gcmd.error) {
      log_error("failed to handle virtio gpu request from zone %d, and request "
                "type is %d, error type is %d",
                vdev->zone_id, gcmd.control_header.type, gcmd.error);
    }
    virtio_gpu_ctrl_response_nodata(
        vdev, &gcmd, gcmd.error ? gcmd.error : VIRTIO_GPU_RESP_OK_NODATA);
  }

  // 处理完毕，不需要iov
  free(gcmd.resp_iov);

  log_debug("------ leaving %s ------", __func__);
}