#include "virtio_gpu.h"
#include "linux/virtio_gpu.h"
#include "log.h"
#include <stdint.h>
#include <string.h>

void virtio_gpu_ctrl_response(VirtIODevice *vdev, GPUCommand *gcmd,
                              GPUControlHeader *resp, size_t resp_len) {
  log_debug("entering %s", __func__);
  // 因为每个response结构体的头部都是GPUControlHeader，因此其地址就是response结构体的地址

  // TODO: 向iov写数据
  // 假设iov全是可写的
  size_t s = buf_to_iov(gcmd->resp_iov, gcmd->resp_iov_cnt, 0, resp, resp_len);

  // 根据命令选择返回vq
  int sel = GPU_CONTROL_QUEUE;

  if (gcmd->control_header.type == VIRTIO_GPU_CMD_UPDATE_CURSOR ||
      gcmd->control_header.type == VIRTIO_GPU_CMD_MOVE_CURSOR) {
    sel = GPU_CURSOR_QUEUE;
  }

  update_used_ring(&vdev->vqs[sel], gcmd->resp_idx, resp_len);

  gcmd->finished = true;
}

void virtio_gpu_ctrl_response_nodata(VirtIODevice *vdev, GPUCommand *gcmd,
                                     enum virtio_gpu_ctrl_type type) {
  log_debug("entering %s", __func__);

  GPUControlHeader resp;

  memset(&resp, 0, sizeof(resp));
  resp.type = type;
  virtio_gpu_ctrl_response(vdev, gcmd, &resp, sizeof(resp));
}

void virtio_gpu_get_display_info(VirtIODevice *vdev, GPUCommand *gcmd) {
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
  }

  virtio_gpu_ctrl_response(vdev, gcmd, &display_info.hdr, sizeof(display_info));
}

void virtio_gpu_get_edid(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_resource_create_2d(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_resource_unref(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_resource_flush(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_set_scanout(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_transfer_to_host_2d(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_resource_attach_backing(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

void virtio_gpu_resource_detach_backing(VirtIODevice *vdev, GPUCommand *gcmd) {
  log_debug("entering %s", __func__);
}

int virtio_gpu_simple_process_cmd(struct iovec *iov, const int iov_cnt,
                                  uint16_t resp_idx, VirtIODevice *vdev) {
  log_debug("entering %s", __func__);

  GPUCommand gcmd;
  gcmd.resp_iov = iov;
  gcmd.resp_iov_cnt = iov_cnt;
  gcmd.resp_idx = resp_idx;
  gcmd.error = 0;
  gcmd.finished = false;

  // 先填充每个请求都有的cmd_hdr
  VIRTIO_GPU_FILL_CMD(iov, iov_cnt, gcmd.control_header);

  // 根据cmd_hdr的类型跳转到对应的处理函数
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
    virtio_gpu_ctrl_response_nodata(
        vdev, &gcmd, gcmd.error ? gcmd.error : VIRTIO_GPU_RESP_OK_NODATA);
  }

  return 0;
}