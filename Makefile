SRC = flash_erase.c nandwrite.c ofgwrite.c ubiformat.c ubiutils-common.c libubigen.c libscan.c libubi.c flashcp.c ubidetach.c ubiupdatevol.c fb.c flash_ubi_jffs2.c flash_ext4.c

SRC_TAR=tar/tar.c \
	tar/libarchive/data_align.c \
	tar/libarchive/data_extract_all.c \
	tar/libarchive/data_extract_to_stdout.c \
	tar/libarchive/data_skip.c \
	tar/libarchive/decompress_bunzip2.c \
	tar/libarchive/filter_accept_reject_list.c \
	tar/libarchive/filter_accept_all.c \
	tar/libarchive/find_list_entry.c \
	tar/libarchive/get_header_tar.c \
	tar/libarchive/header_list.c \
	tar/libarchive/header_skip.c \
	tar/libarchive/header_verbose_list.c \
	tar/libarchive/init_handle.c \
	tar/libarchive/open_transformer.c \
	tar/libarchive/seek_by_jump.c \
	tar/libarchive/seek_by_read.c \
	tar/libarchive/unsafe_prefix.c \
	tar/libbb/appletlib.c \
	tar/libbb/bb_strtonum.c \
	tar/libbb/compare_string_array.c \
	tar/libbb/concat_path_file.c \
	tar/libbb/copyfd.c \
	tar/libbb/crc32.c \
	tar/libbb/default_error_retval.c \
	tar/libbb/full_write.c \
	tar/libbb/getopt32.c \
	tar/libbb/last_char_is.c \
	tar/libbb/llist.c \
	tar/libbb/makedev.c \
	tar/libbb/make_directory.c \
	tar/libbb/messages.c \
	tar/libbb/mode_string.c \
	tar/libbb/perror_msg.c \
	tar/libbb/ptr_to_globals.c \
	tar/libbb/read.c \
	tar/libbb/read_printf.c \
	tar/libbb/safe_poll.c \
	tar/libbb/safe_strncpy.c \
	tar/libbb/safe_write.c \
	tar/libbb/signals.c \
	tar/libbb/time.c \
	tar/libbb/verror_msg.c \
	tar/libbb/xatonum.c \
	tar/libbb/xfunc_die.c \
	tar/libbb/xfuncs.c \
	tar/libbb/xfuncs_printf.c


OBJ = $(SRC:.c=.o)
OBJ_TAR = $(SRC_TAR:.c=.o)

OUT = ofgwrite_bin

LDFLAGS= -Llib -lmtd -static

LIBSRC = ./lib/libmtd.c ./lib/libmtd_legacy.c ./lib/libcrc32.c ./lib/libfec.c

LIBOBJ = $(LIBSRC:.c=.o)

OUT_LIB = ./lib/libmtd.a

CFLAGS ?= -O2
CFLAGS += -I./include -I./tar/include -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

CC ?= gcc
AR ?= ar

.SUFFIXES: .cpp

default: $(OUT_LIB) $(OUT)

.cpp.o:
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_LIB): $(LIBOBJ)
	$(AR) rcs $(OUT_LIB) $(LIBOBJ)

$(OUT): $(OBJ) $(OBJ_TAR) $(OUT_LIB)
	$(CC) -o $@ $(OBJ) $(OBJ_TAR) $(LDFLAGS)

clean:
	rm -f $(LIBOBJ) $(OUT_LIB) $(OBJ) $(OBJ_TAR) $(OUT)
