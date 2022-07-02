#include <sys/stat.h>

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

void handle_busybox_fatal_error();

enum RootfsTypeEnum
{
	UNKNOWN, UBIFS, JFFS2, EXT4
};

enum FlashModeTypeEnum
{
	FLASH_MODE_UNKNOWN, MTD, TARBZ2, TARBZ2_MTD
};

extern enum FlashModeTypeEnum kernel_flash_mode;
extern enum FlashModeTypeEnum rootfs_flash_mode;
