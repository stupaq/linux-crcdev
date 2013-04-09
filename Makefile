CC          := gcc -m32 -Wall -Wextra -pedantic -std=gnu99
CFLAGS      := -fPIC
LDFLAGS     :=

SOURCES     := $(wildcard *.c)
DEPENDS     := $(subst .c,.d,$(SOURCES))
MAINOBJS	  := $(subst .c,.o,$(shell grep -l "MAKE MAIN" $(SOURCES)))
SHAREDOBJS  := $(subst .c,.o,$(shell grep -l "MAKE SHARED LIBRARY" $(SOURCES)))
MAIN        := $(subst .o,,$(MAINOBJS))
SHARED			:= $(patsubst %.o,lib%.so,$(SHAREDOBJS))

all: $(DEPENDS) $(MAIN) $(SHARED)

$(DEPENDS) : %.d : %.c
	$(CC) -MT $(<:.c=.o) -MM $< > $@
	@echo -e "\t"$(CC) -c $(CFLAGS) $< -o $(<:.c=.o) >> $@

$(MAIN) : % : %.o
	$(CC) $(LDFLAGS) -o $@ $^

$(SHARED) : lib%.so : %.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname=$@ -o $@ $^

-include $(DEPENDS)

clean:
	-rm -f *.o $(DEPENDS) $(MAIN) $(SHARED) $(MAINOBJS) $(SHAREDOBJS)

.PHONY: clean qemu
