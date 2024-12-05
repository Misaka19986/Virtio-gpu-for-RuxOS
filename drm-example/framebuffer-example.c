#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
  // 打开帧缓冲设备
  int fb_fd = open("/dev/fb0", O_RDWR);
  if (fb_fd == -1) {
    perror("Error: cannot open framebuffer device");
    return 1;
  }

  // 获取可变屏幕信息
  struct fb_var_screeninfo vinfo;
  if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
    perror("Error reading variable information");
    close(fb_fd);
    return 1;
  }

  // 计算屏幕大小（字节）
  size_t screensize =
      vinfo.yres_virtual * vinfo.xres_virtual * vinfo.bits_per_pixel / 8;

  // 将帧缓冲设备映射到内存
  unsigned short *fb_ptr = (unsigned short *)mmap(
      0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
  if ((int)fb_ptr == -1) {
    perror("Error: failed to map framebuffer device to memory");
    close(fb_fd);
    return 1;
  }

  // 创建一个2x2像素的RGB565缓冲区
  unsigned short buffer[2][2] = {
      {0xF800, 0x07E0}, // 红色，绿色
      {0x001F, 0xFFFF}  // 蓝色，白色
  };

  // 将缓冲区的数据写入帧缓冲区
  memcpy(fb_ptr, buffer, sizeof(buffer));

  // 清理
  munmap(fb_ptr, screensize);
  close(fb_fd);

  return 0;
}
