obj-m += sysmon.o

KERNELDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	rm -rf *.o *.mod.c *.mod.o *.ko *.ko.cmd *.o.cmd *.mod .sysmon* *.order *.symvers
