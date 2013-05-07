# Kbuild
obj-m += crcdev.o
crcdev-objs := module.o pci.o concepts.o interrupts.o chrdev.o sysfs.o

# Makefile
KDIR ?= /lib/modules/`uname -r`/build
MAKE_OPTS := -C $(KDIR) M=$(PWD) W=1 C=2 CF="-D__CHECK_ENDIAN__"

default:
	$(MAKE) $(MAKE_OPTS)

install:
	$(MAKE) $(MAKE_OPTS) modules_install

clean:
	$(MAKE) $(MAKE_OPTS) clean

help:
	$(MAKE) $(MAKE_OPTS) help
