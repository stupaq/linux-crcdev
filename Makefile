# Kbuild
obj-m += crcdev.o
crcdev-objs := module.o pci.o concepts.o interrupts.o chrdev.o sysfs.o fileops.o

# Debug
#CFLAGS_interrupts.o += -DCRC_DEBUG
#CFLAGS_fileops.o += -DCRC_DEBUG

# Makefile
KDIR ?= /lib/modules/`uname -r`/build
MAKE_OPTS := -C $(KDIR) M=$(PWD) W=1 C=2 CF="-D__CHECK_ENDIAN__"

default:
	$(MAKE) $(MAKE_OPTS)

install:
	$(MAKE) $(MAKE_OPTS) modules_install

clean:
	$(MAKE) $(MAKE_OPTS) clean
	$(MAKE) -C test clean

help:
	$(MAKE) $(MAKE_OPTS) help

test:
	$(MAKE) -C test
	./test/simple
	./test/long
	./test/thread
	./test/mux
	./test/rmux

.PHONY: test
