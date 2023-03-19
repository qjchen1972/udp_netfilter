ifneq ($(KERNELRELEASE),)
    forward-objs := proc.o setup.o vip.o udp_forward.o streambuf.o
    obj-m:=forward.o	 
else
    KERNELDIR:=/lib/modules/$(shell uname -r)/build
    PWD:=$(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR)  M=$(PWD)  modules
clean:
	rm -rf *.o *.mod.c *.mod.o *.ko
endif
