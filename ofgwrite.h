#include <sys/stat.h>

struct stat kernel_file_stat;
struct stat rootfs_file_stat;

int multiboot_partition;
char current_rootfs_device[1000];

void handle_busybox_fatal_error();

enum RootfsTypeEnum
{
	UNKNOWN, UBIFS, JFFS2, EXT4
};
