#include <sys/stat.h>

struct stat kernel_file_stat;
struct stat rootfs_file_stat;

char kernel_device_arg[1000];
char rootfs_device_arg[1000];
char kernel_device[1000];
char rootfs_device[1000];
char rootfs_sub_dir[1000];

int found_kernel_device;
int found_rootfs_device;
int user_kernel;
int user_rootfs;
int multiboot_partition;
char current_rootfs_device[1000];
char current_kernel_device[1000];
char current_rootfs_sub_dir[1000];

void handle_busybox_fatal_error();

enum RootfsTypeEnum
{
	UNKNOWN, UBIFS, JFFS2, EXT4
};

enum FlashModeTypeEnum
{
	FLASH_MODE_UNKNOWN, MTD, TARBZ2
};

enum FlashModeTypeEnum kernel_flash_mode;
enum FlashModeTypeEnum rootfs_flash_mode;
