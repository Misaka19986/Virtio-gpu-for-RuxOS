obj-m += hvisor.o
obj-m += ivc.o
ifeq ($(ARCH), arm64)
	COMPILE := aarch64-none-linux-gnu-
else ifeq ($(ARCH), riscv)
	COMPILE := riscv64-unknown-linux-gnu-
endif

EXTRA_CFLAGS += "-I$(PWD)/include  -fno-stack-protector"

ivc-y := ivc_driver.o hivc_template.dtb.o

.SECONDARY: \
        $(obj)/hivc_template.dtb.S \
        $(obj)/hivc_template.dtb

.PHONY: all clean

all:
	make ARCH=$(ARCH) CROSS_COMPILE=$(COMPILE) -C $(KDIR) M=$(shell pwd) EXTRA_CFLAGS=$(EXTRA_CFLAGS) modules
 
clean:
	make -C $(KDIR) M=$(shell pwd) clean