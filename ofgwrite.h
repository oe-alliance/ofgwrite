#include <sys/stat.h>
#include <stdio.h>

extern struct stat kernel_file_stat;
extern struct stat rootfs_file_stat;

extern char kernel_device_arg[1000];
extern char rootfs_device_arg[1000];
extern char kernel_device[1000];
extern char rootfs_device[1000];
extern char rootfs_sub_dir[1000];

extern int found_kernel_device;
extern int found_rootfs_device;
extern int user_kernel;
extern int user_rootfs;
extern int rootsubdir_check;
extern int multiboot_partition;
extern char current_rootfs_device[1000];
extern char current_kernel_device[1000];
extern char current_rootfs_sub_dir[1000];
extern char ubi_fs_name[1000];
extern char ubi_loop_device[1000];
extern int loop_mtd_device;
extern char nfi_filename[1000];
extern char nfi_path[1000];

void handle_busybox_fatal_error();

enum RootfsTypeEnum
{
	UNKNOWN, UBIFS, JFFS2, EXT4, EXT3
};

enum FlashModeTypeEnum
{
	FLASH_MODE_UNKNOWN, MTD, TARBZ2, TARBZ2_MTD, UBI_LOOP_SUBDIR, TARXZ_UBI
};
// TARBZ2, TARBZ2_MTD is also used for xz compressed rootfs

extern enum FlashModeTypeEnum kernel_flash_mode;
extern enum FlashModeTypeEnum rootfs_flash_mode;

enum ImageTypeEnum
{
	IMAGE_UNKNOWN, UBI, TAR_BASED, TAR_UBI
};

extern enum ImageTypeEnum image_type;

void my_printf(const char *format, ...);
void my_fprintf(FILE* stream, const char *format, ...);

int set_step(char*);
void set_step_without_incr(char* str);
void set_step_progress(int percent);
void set_overall_progress(int step);
void set_error_text(char* str);
void set_error_text1(char* str);
void set_error_text2(char* str);
int init_framebuffer(int steps);
void close_framebuffer();
void set_overall_text(char* str);
int show_main_window(int show_background_image, const char* version);

int flash_ext4_kernel(char* device, char* filename, off_t kernel_file_size, int quiet, int no_write);
int flash_unpack_rootfs(char* filename, int quiet, int no_write);
int flash_ubi_jffs2_kernel(char* device, char* filename, int quiet, int no_write);
int flash_ubi_jffs2_rootfs(char* device, char* filename, enum RootfsTypeEnum rootfs_type, int quiet, int no_write);
int flash_erase_main(int argc, char **argv);
int nandwrite_main(int argc, char **argv);
int ubiformat_main(int argc, char **argv);
int ubidetach_main(int argc, char **argv);
int ubiattach_main(int argc, char **argv);
int flashcp_main(int argc, char **argv);
int cp_main(int argc, char **argv);
int losetup_main(int argc, char **argv);

