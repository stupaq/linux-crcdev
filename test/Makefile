BINARIES	:= simple long thread mux rmux
EXTRA_SRC	:= ../userland/crcdev_if.c gen.c

CFLAGS		:= -pthread -Wall -I../userland

all: $(BINARIES)

%: %.c $(EXTRA_SRC)
	gcc $(CFLAGS) $< $(EXTRA_SRC) -o $@

clean:
	rm -rf $(BINARIES)
