# 编译器
CC = aarch64-linux-gnu-gcc

# 编译器标志 
CFLAGS = -Wall -Werror -O2 -DTARGET_ARM64 -DLOG_USE_COLOR

# 包含目录，可以添加多个目录，用空格分隔
INCLUDE_DIRS = /usr/aarch64-linux-gnu/include

# 库目录
LIB_DIRS = /usr/aarch64-linux-gnu/lib

# 链接的库
LIBS = -lSDL2

# 目标文件
TARGET = sdl-example

# 源文件
SRCS = $(wildcard *.c)

# 目标文件
OBJS = $(SRCS:.c=.o)

# 依赖文件
DEPS = $(SRCS:.c=.d)

# 构建包含目录参数
INCLUDE_PARAMS = $(foreach d, $(INCLUDE_DIRS), -I$d)

# 构建库目录参数
LIB_PARAMS = $(foreach d, $(LIB_DIRS), -L$d)

# 默认目标
all: $(TARGET)

# 链接目标文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDE_PARAMS) $(LIB_PARAMS) $(LIBS)

# 编译源文件并生成依赖文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE_PARAMS)
	$(CC) -MM $(CFLAGS) $< $(INCLUDE_PARAMS) > $(@:.o=.d)

# 包含依赖文件
-include $(DEPS)

# 清理
clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

.PHONY: all clean
