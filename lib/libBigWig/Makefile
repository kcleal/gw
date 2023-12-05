CC ?= gcc
AR ?= ar
RANLIB ?= ranlib
CFLAGS ?= -g -Wall -O3 -Wsign-compare
LIBS = -lm -lz -lcurl
EXTRA_CFLAGS_PIC = -fpic

.PHONY: all clean lib

.SUFFIXES: .c .o .pico

all: lib

lib: lib-static

lib-static: libBigWig.a


OBJS = io.o bwValues.o bwRead.o bwStats.o bwWrite.o

.c.o:
	$(CC) -I. $(CFLAGS) $(INCLUDES) -c -o $@ $<

.c.pico:
	$(CC) -I. $(CFLAGS) $(INCLUDES) $(EXTRA_CFLAGS_PIC) -c -o $@ $<

libBigWig.a: $(OBJS)
	-@rm -f $@
	$(AR) -rcs $@ $(OBJS)
	$(RANLIB) $@

clean:
	rm -f *.o libBigWig.a
