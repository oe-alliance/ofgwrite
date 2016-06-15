#include <sys/stat.h>

struct stat kernel_file_stat;
struct stat rootfs_file_stat;

int multiboot_partition;

enum RootfsTypeEnum
{
	UNKNOWN, UBIFS, JFFS2, EXT4
};
