obj-m    := main.o

KDIR    := /lib/modules/$(shell uname -r)/build

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
