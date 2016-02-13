SRC = flash_erase.c nandwrite.c ofgwrite.c ubiformat.c ubiutils-common.c libubigen.c libscan.c libubi.c flashcp.c ubidetach.c ubiupdatevol.c fb.c flash_ubi_jffs2.c flash_ext4.c

SRC_BUSYBOX= busybox/fdisk.c \
	busybox/fdisk_gpt.c \
	busybox/fuser.c \
	busybox/ps.c \
	busybox/rm.c \
	busybox/tar.c \
	busybox/libarchive/data_align.c \
	busybox/libarchive/data_extract_all.c \
	busybox/libarchive/data_extract_to_stdout.c \
	busybox/libarchive/data_skip.c \
	busybox/libarchive/decompress_bunzip2.c \
	busybox/libarchive/filter_accept_reject_list.c \
	busybox/libarchive/filter_accept_all.c \
	busybox/libarchive/find_list_entry.c \
	busybox/libarchive/get_header_tar.c \
	busybox/libarchive/header_list.c \
	busybox/libarchive/header_skip.c \
	busybox/libarchive/header_verbose_list.c \
	busybox/libarchive/init_handle.c \
	busybox/libarchive/open_transformer.c \
	busybox/libarchive/seek_by_jump.c \
	busybox/libarchive/seek_by_read.c \
	busybox/libarchive/unsafe_prefix.c \
	busybox/libbb/appletlib.c \
	busybox/libbb/auto_string.c \
	busybox/libbb/bb_strtonum.c \
	busybox/libbb/compare_string_array.c \
	busybox/libbb/concat_path_file.c \
	busybox/libbb/concat_subpath_file.c \
	busybox/libbb/copyfd.c \
	busybox/libbb/crc32.c \
	busybox/libbb/default_error_retval.c \
	busybox/libbb/full_write.c \
	busybox/libbb/getopt32.c \
	busybox/libbb/get_last_path_component.c \
	busybox/libbb/human_readable.c \
	busybox/libbb/last_char_is.c \
	busybox/libbb/lineedit.c \
	busybox/libbb/llist.c \
	busybox/libbb/makedev.c \
	busybox/libbb/make_directory.c \
	busybox/libbb/messages.c \
	busybox/libbb/mode_string.c \
	busybox/libbb/perror_msg.c \
	busybox/libbb/procps.c \
	busybox/libbb/ptr_to_globals.c \
	busybox/libbb/read.c \
	busybox/libbb/read_printf.c \
	busybox/libbb/remove_file.c \
	busybox/libbb/safe_poll.c \
	busybox/libbb/safe_strncpy.c \
	busybox/libbb/safe_write.c \
	busybox/libbb/signals.c \
	busybox/libbb/skip_whitespace.c \
	busybox/libbb/time.c \
	busybox/libbb/u_signal_names.c \
	busybox/libbb/verror_msg.c \
	busybox/libbb/wfopen.c \
	busybox/libbb/xatonum.c \
	busybox/libbb/xfunc_die.c \
	busybox/libbb/xfuncs.c \
	busybox/libbb/xfuncs_printf.c \
	busybox/libbb/xreadlink.c


OBJ = $(SRC:.c=.o)
OBJ_BUSYBOX = $(SRC_BUSYBOX:.c=.o)

OUT = ofgwrite_bin

LDFLAGS= -Llib -lmtd -static

LIBSRC = ./lib/libmtd.c ./lib/libmtd_legacy.c ./lib/libcrc32.c ./lib/libfec.c

LIBOBJ = $(LIBSRC:.c=.o)

OUT_LIB = ./lib/libmtd.a

CFLAGS ?= -O2
CFLAGS += -I./include -I./busybox/include -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

CC ?= gcc
AR ?= ar

.SUFFIXES: .cpp

default: $(OUT_LIB) $(OUT)

.cpp.o:
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_LIB): $(LIBOBJ)
	$(AR) rcs $(OUT_LIB) $(LIBOBJ)

$(OUT): $(OBJ) $(OBJ_BUSYBOX) $(OUT_LIB)
	$(CC) -o $@ $(OBJ) $(OBJ_BUSYBOX) $(LDFLAGS)

clean:
	rm -f $(LIBOBJ) $(OUT_LIB) $(OBJ) $(OBJ_BUSYBOX) $(OUT)
