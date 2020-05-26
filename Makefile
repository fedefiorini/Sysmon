obj-m += sysmon.o
#obj-m += sysmon_random.o
obj-m += sysmon_reuse_distance.o
#obj-m += sysmon_random_reuse_distance.o
obj-m += sysmon_RD_WR.o
#obj-m += sysmon_bank_balance.o
# Ignore random ones plus the bank balance (still unchanged)

KERNELDIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	rm -rf *.o *.mod.c *.mod.o *.ko *.ko.cmd *.o.cmd *.mod
