obj-m += vtfs.o vtfs_net.o
vtfs-y := source/vtfs.o source/storage_ram.o
vtfs_net-y := source/vtfs_net.o source/storage_net.o source/http.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
