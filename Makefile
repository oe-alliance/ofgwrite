SRC = flash_erase.c nandwrite.c ofgwrite.c ubiformat.c ubiutils-common.c libubigen.c libscan.c libubi.c flashcp.c ubidetach.c ubiupdatevol.c fb.c flash_ubi_jffs2.c

OBJ = $(SRC:.c=.o)

OUT = ofgwrite_bin

LDFLAGS= -Llib -lmtd -static

LIBSRC = ./lib/libmtd.c ./lib/libmtd_legacy.c ./lib/libcrc32.c ./lib/libfec.c

LIBOBJ = $(LIBSRC:.c=.o)

OUT_LIB = ./lib/libmtd.a

CFLAGS ?= -O2
CFLAGS += -I./include

CC ?= gcc
AR ?= ar

.SUFFIXES: .cpp

default: $(OUT_LIB) $(OUT)

.cpp.o:
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_LIB): $(LIBOBJ)
	$(AR) rcs $(OUT_LIB) $(LIBOBJ)

$(OUT): $(OBJ) $(OUT_LIB)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(LIBOBJ) $(OUT_LIB) $(OBJ) $(OUT)
