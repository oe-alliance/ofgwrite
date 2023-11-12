#include "ofgwrite.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <getopt.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <syslog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mntent.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/evp.h>

#include "busybox/include/libbb.h"

#define SHA_DIGEST_LENGTH 20

typedef struct {
    // ... (other header fields)
    size_t kernel_size;
    size_t ramdisk_size;
    size_t second_size;
    unsigned char id[SHA_DIGEST_LENGTH]; // SHA-1 hash result
    // ... (other header fields)
} Header;

typedef struct boot_img_hdr boot_img_hdr;

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512
#define BOOT_EXTRA_ARGS_SIZE 1024

struct boot_img_hdr
{
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;  /* size in bytes */
    uint32_t kernel_addr;  /* physical load addr */
    uint32_t ramdisk_size; /* size in bytes */
    uint32_t ramdisk_addr; /* physical load addr */
    uint32_t second_size;  /* size in bytes */
    uint32_t second_addr;  /* physical load addr */
    uint32_t tags_addr;    /* physical addr for kernel tags */
    uint32_t page_size;    /* flash page size we assume */
    uint32_t unused[2];    /* future expansion: should be 0 */
    uint8_t name[BOOT_NAME_SIZE]; /* asciiz product name */
    uint8_t cmdline[BOOT_ARGS_SIZE];
    uint32_t id[8]; /* timestamp / checksum / sha1 / etc */
    /* Supplemental command line data; kept here to maintain
     * binary compatibility with older versions of mkbootimg */
    uint8_t extra_cmdline[BOOT_EXTRA_ARGS_SIZE];
} __attribute__((packed));


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
int user_slotname;
int rootsubdir_check;
int multiboot_partition;
char kexec_mode[1000];
char current_rootfs_device[1000];
char current_kernel_device[1000];
char current_rootfs_sub_dir[1000];
char ubi_fs_name[1000];

enum FlashModeTypeEnum kernel_flash_mode;
enum FlashModeTypeEnum rootfs_flash_mode;

int android = 0;
int dreamcard = 1;
int flash_kernel  = 0;
int flash_rootfs  = 0;
int no_write      = 0;
int force_e2_stop = 0;
int quiet         = 0;
int show_help     = 0;
int newroot_mounted = 0;
char kernel_filename[1000];
char rootfs_filename[1000];
char rootfs_mount_point[1000];
char slotname[1000];
char *boxname = NULL;
enum RootfsTypeEnum rootfs_type;
int stop_e2_needed = 1;

const char ofgwrite_version[] = "4.6.7";

struct struct_mountlist
{
	char* dir;
	struct struct_mountlist *next;
} *mountlist, *mountlist_entry;


static unsigned char padding[16384] = { 0, };

static void print_id(const uint8_t *id, size_t id_len) {
    my_printf("0x");
    for (unsigned i = 0; i < id_len; i++) {
        my_printf("%02x", id[i]);
    }
    my_printf("\n");
}

int write_padding(int fd, unsigned pagesize, unsigned itemsize)
{
    unsigned pagemask = pagesize - 1;
    ssize_t count;

    if((itemsize & pagemask) == 0) {
        return 0;
    }

    count = pagesize - (itemsize & pagemask);

    if(write(fd, padding, count) != count) {
        return -1;
    } else {
        return 0;
    }
}

static void *load_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

int generate_boot_image(const char *kernelPath)
{
    boot_img_hdr hdr;
    char *kernel_fn = NULL;
    void *kernel_data = NULL;
    char *ramdisk_fn = NULL;
    void *ramdisk_data = NULL;
    int second_fn_len = snprintf(NULL, 0, "/oldroot_remount/boot/dream%s.dtb", boxname);
    char *second_fn = (char *)malloc(second_fn_len + 1);
    void *second_data = NULL;
    int cmdline_len = snprintf(NULL, 0, "console=ttyS0,1000000 root=%s rootwait rootfstype=ext4 no_console_suspend", rootfs_device);
    char *cmdline = (char *)malloc(cmdline_len + 1);
    char *bootimg = NULL;
    char *board = "";
    uint32_t pagesize = 2048;
    int fd;
    const uint8_t* sha;
    uint32_t base           = 0x10000000U;
    uint32_t kernel_offset  = 0x00008000U;
    uint32_t ramdisk_offset = 0x01000000U;
    uint32_t second_offset  = 0x00f00000U;
    uint32_t tags_offset    = 0x00000100U;
    size_t cmdlen;

    memset(&hdr, 0, sizeof(hdr));


    bool get_id = false;
    bootimg = kernelPath;
    kernel_fn = "/oldroot_remount/boot/Image.gz-4.9";
    snprintf(second_fn, second_fn_len + 1, "/oldroot_remount/boot/dream%s.dtb", boxname);
    snprintf(cmdline, cmdline_len + 1,"console=ttyS0,1000000 root=%s rootwait rootfstype=ext4 no_console_suspend", rootfs_device);
    base = strtoul("0", 0, 16);
    kernel_offset = strtoul("0x1080000", 0, 16);
    second_offset = strtoul("0x1000000", 0, 16);
    board = boxname;
    my_printf("bootimage bootimg:%s\n",bootimg);
    my_printf("bootimage kernel_fn:%s\n",kernel_fn);
    my_printf("bootimage second_fn:%s\n",second_fn);
    my_printf("bootimage cmdline:%s\n",cmdline);
    my_printf("bootimage base:%08X\n",base);
    my_printf("bootimage kernel_offset:%08X\n",kernel_offset);
    my_printf("bootimage second_offset:%08X\n",second_offset);
    my_printf("bootimage board:%s\n",board);

    hdr.page_size = pagesize;

    hdr.kernel_addr =  base + kernel_offset;
    hdr.ramdisk_addr = base + ramdisk_offset;
    hdr.second_addr =  base + second_offset;
    hdr.tags_addr =    base + tags_offset;

    if(strlen(board) >= BOOT_NAME_SIZE) {
        my_printf("error: board name too large\n");
        return EXIT_FAILURE;;
    }

    strcpy((char *) hdr.name, board);
    memcpy(hdr.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);
    cmdlen = strlen(cmdline);
    if(cmdlen > (BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE - 2)) {
        my_printf("error: kernel commandline too large\n");
        return EXIT_FAILURE;;
    }
    /* Even if we need to use the supplemental field, ensure we
     * are still NULL-terminated */
    strncpy((char *)hdr.cmdline, cmdline, BOOT_ARGS_SIZE - 1);
    hdr.cmdline[BOOT_ARGS_SIZE - 1] = '\0';
    if (cmdlen >= (BOOT_ARGS_SIZE - 1)) {
        cmdline += (BOOT_ARGS_SIZE - 1);
        strncpy((char *)hdr.extra_cmdline, cmdline, BOOT_EXTRA_ARGS_SIZE);
    }
    kernel_data = load_file(kernel_fn, &hdr.kernel_size);
    if(kernel_data == 0) {
        my_printf("error: could not load kernel '%s'\n", kernel_fn);
        return EXIT_FAILURE;;
    }
        ramdisk_data = 0;
        hdr.ramdisk_size = 0;

    if(second_fn) {
        second_data = load_file(second_fn, &hdr.second_size);
        if(second_data == 0) {
            my_printf("error: could not load secondstage '%s'\n", second_fn);
            return EXIT_FAILURE;;
        }
    }
    /* put a hash of the contents in the header so boot images can be
     * differentiated based on their first 2k.
     */
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned char hash[SHA_DIGEST_LENGTH];

    // Initialize OpenSSL library
    OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
    // Create a context for the hash operation
    mdctx = EVP_MD_CTX_new();

    if (!mdctx) {
        my_printf("EVP_MD_CTX_new() failed.\n");
        return EXIT_FAILURE;;
    }

    // Specify the hash algorithm (SHA-1 in this case)
    md = EVP_sha1();

    // Initialize the context with the chosen hash algorithm
    if (1 != EVP_DigestInit_ex(mdctx, md, NULL)) {
        my_printf("EVP_DigestInit_ex() failed.\n");
        return EXIT_FAILURE;;
    }
    // Update the hash context with your data
    // Assuming kernel_data, ramdisk_data, second_data, and hdr are properly defined
    // hdr.kernel_size, hdr.ramdisk_size, and hdr.second_size are also assumed to be set correctly
    EVP_DigestUpdate(mdctx, kernel_data, hdr.kernel_size);
    EVP_DigestUpdate(mdctx, &hdr.kernel_size, sizeof(hdr.kernel_size));
    EVP_DigestUpdate(mdctx, ramdisk_data, hdr.ramdisk_size);
    EVP_DigestUpdate(mdctx, &hdr.ramdisk_size, sizeof(hdr.ramdisk_size));
    EVP_DigestUpdate(mdctx, second_data, hdr.second_size);
    EVP_DigestUpdate(mdctx, &hdr.second_size, sizeof(hdr.second_size));
    // Finalize the hash and obtain the result
    if (1 != EVP_DigestFinal_ex(mdctx, hash, NULL)) {
        my_printf("EVP_DigestFinal_ex() failed.\n");
        return EXIT_FAILURE;;
    }

    // Copy the hash result to hdr.id
    memcpy(hdr.id, hash, SHA_DIGEST_LENGTH);

    // Check if the kernel.img file already exists
    if (access(bootimg, F_OK) != -1) {
        // If it exists, remove it
        if (remove(bootimg) != 0) {
            my_printf("Failed to delete existing %s\n", bootimg);
           return EXIT_FAILURE;
        }
        my_printf("Existing %s removed.\n", bootimg);
    }

    fd = open(bootimg, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd < 0) {
        my_printf("error: could not create '%s'\n", bootimg);
        return EXIT_FAILURE;;
    }
    if(write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) goto fail;
    if(write_padding(fd, pagesize, sizeof(hdr))) goto fail;
    if(write(fd, kernel_data, hdr.kernel_size) != (ssize_t) hdr.kernel_size) goto fail;
    if(write_padding(fd, pagesize, hdr.kernel_size)) goto fail;
    if(write(fd, ramdisk_data, hdr.ramdisk_size) != (ssize_t) hdr.ramdisk_size) goto fail;
    if(write_padding(fd, pagesize, hdr.ramdisk_size)) goto fail;
    if(second_data) {
        if(write(fd, second_data, hdr.second_size) != (ssize_t) hdr.second_size) goto fail;
        if(write_padding(fd, pagesize, hdr.second_size)) goto fail;
    }

    if (get_id) {
        print_id((uint8_t *) hdr.id, sizeof(hdr.id));
    }
    my_printf("Successfully writing '%s'\n", bootimg);
    return 0;

fail:
    unlink(bootimg);
    close(fd);
    my_printf("error: failed writing '%s'\n", bootimg);
    return EXIT_FAILURE;;
}

void my_printf(char const *fmt, ...)
{
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	// print to console
	vprintf(fmt, ap);
	va_end(ap);

	// print to syslog
	vsyslog(LOG_INFO, fmt, ap2);
	va_end(ap2);
}

void my_fprintf(FILE * f, char const *fmt, ...)
{
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	// print to file (normally stdout or stderr)
	vfprintf(f, fmt, ap);
	va_end(ap);

	// print to syslog
	vsyslog(LOG_INFO, fmt, ap2);
	va_end(ap2);
}

void printUsage()
{
	my_printf("Usage: ofgwrite <parameter> <image_directory>\n");
	my_printf("Options:\n");
	my_printf("   -a --android          create Android boot image header\n");
	my_printf("   -k --kernel           flash kernel with automatic device recognition(default)\n");
	my_printf("   -kmtdx --kernel=mtdx  use mtdx device for kernel flashing\n");
	my_printf("   -ksdx --kernel=sdx    use sdx device for kernel flashing\n");
	my_printf("   -kmmcblkxpx --kernel=mmcblkxpx  use mmcblkxpx device for kernel flashing\n");
	my_printf("   -r --rootfs           flash rootfs with automatic device recognition(default)\n");
	my_printf("   -rmtdy --rootfs=mtdy  use mtdy device for rootfs flashing\n");
	my_printf("   -rmmcblkxpx --rootfs=mmcblkxpx  use mmcblkxpx device for rootfs flashing\n");
	my_printf("   -sNN --slotname=NN    user defined slot name\n");
	my_printf("   -mx --multi=x         flash multiboot partition x (x= 1, 2, 3,...). Only supported by some boxes.\n");
	my_printf("   -n --nowrite          show only found image and mtd partitions (no write)\n");
	my_printf("   -f --force            force kill e2\n");
	my_printf("   -q --quiet            show less output\n");
	my_printf("   -h --help             show help\n");
}

char* ReadProcEntry(char *filename)
{
	FILE *boxtype_file = fopen(filename,"r");
	char boxtype_name[20];
	char *real_boxtype_name = NULL;
	char c;
	int i = 0;

	if(boxtype_file)
	{
		while ((c = fgetc(boxtype_file)) != EOF && i < sizeof(boxtype_name) - 2)
		{
			if (c == '\n')
			{
				i--;
				break;
			}
			boxtype_name[i] = c;
			i++;
		}
		boxtype_name[i+1] = '\0';
		real_boxtype_name = malloc(strlen(boxtype_name) + 1);
		if (real_boxtype_name)
			strcpy(real_boxtype_name, boxtype_name);

		fclose(boxtype_file);
	}
	else
		my_printf("Can not open this proc entry!\n");

	return real_boxtype_name;
}

int find_image_files(char* p)
{
	DIR *d;
	struct dirent *entry;
	char path[4097];

	if (realpath(p, path) == NULL)
	{
		my_printf("Searching image files: Error path couldn't be resolved\n");
		return 0;
	}
	my_printf("Searching image files in %s resolved to %s\n", p, path);
	kernel_filename[0] = '\0';
	rootfs_filename[0] = '\0';

	// add / to the end of the path
	if (path[strlen(path)-1] != '/')
	{
		path[strlen(path)+1] = '\0';
		path[strlen(path)] = '/';
	}

	d = opendir(path);

	if (!d)
	{
		perror("Error reading image_directory");
		my_printf("\n");
		return 0;
	}

	do
	{
		entry = readdir(d);
		if (entry)
		{
			if ((strstr(entry->d_name, "kernel") != NULL
			  && strstr(entry->d_name, ".bin")   != NULL)			// ET-xx00, XP1000, VU boxes, DAGS boxes
			 || strcmp(entry->d_name, "uImage") == 0)				// Spark boxes
			{
				strcpy(kernel_filename, path);
				strcpy(&kernel_filename[strlen(path)], entry->d_name);
				stat(kernel_filename, &kernel_file_stat);
				my_printf("Found kernel file: %s\n", kernel_filename);
			}
			if (strcmp(entry->d_name, "rootfs.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "root_cfe_auto.bin") == 0		// Solo2
			 || strcmp(entry->d_name, "root_cfe_auto.jffs2") == 0	// other VU boxes
			 || strcmp(entry->d_name, "oe_rootfs.bin") == 0			// DAGS boxes
			 || strcmp(entry->d_name, "e2jffs2.img") == 0			// Spark boxes
			 || strcmp(entry->d_name, "rootfs.tar.bz2") == 0		// solo4k
			 || strcmp(entry->d_name, "rootfs.ubi") == 0			// Zgemma H9
			 || strcmp(entry->d_name, "rootfs.tar.xz") == 0		// dream
			 || strcmp(entry->d_name, "rootfs-one.tar.bz2") == 0		// dreamone
			 || strcmp(entry->d_name, "rootfs-two.tar.bz2") == 0)		// dreamtwo
			{
				strcpy(rootfs_filename, path);
				strcpy(&rootfs_filename[strlen(path)], entry->d_name);
				stat(rootfs_filename, &rootfs_file_stat);
				my_printf("Found rootfs file: %s\n", rootfs_filename);
			}
		}
	} while (entry);

	closedir(d);

	return 1;
}

int read_args(int argc, char *argv[])
{
	int option_index = 0;
	int opt;
	char *endptr;
	long val;
	static const char *short_options = "ak::r::ns:m:fqh";
	static const struct option long_options[] = {
												{"android"  , no_argument, NULL, 'a'},
												{"kernel"    , optional_argument, NULL, 'k'},
												{"rootfs"    , optional_argument, NULL, 'r'},
												{"nowrite"   , no_argument      , NULL, 'n'},
												{"slotname"  , required_argument, NULL, 's'},
												{"multi"     , required_argument, NULL, 'm'},
												{"force"     , no_argument      , NULL, 'f'},
												{"quiet"     , no_argument      , NULL, 'q'},
												{"help"      , no_argument      , NULL, 'h'},
												{NULL        , no_argument      , NULL,  0} };

	strcpy(slotname, "linuxrootfs");
	multiboot_partition = -1;
	user_kernel = 0;
	user_rootfs = 0;
	user_slotname = 0;
	android = 0;
	rootsubdir_check = 0;

	while ((opt= getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
	{
		switch (opt)
		{
			case 'a':
				boxname = ReadProcEntry("/proc/stb/info/model");
				my_printf("Boxname detectet: %s\n", boxname);
				android = 1;
				break;
			case 'k':
				flash_kernel = 1;
				if (optarg)
				{
					if ((!strncmp(optarg, "mtd", 3)) || (!strncmp(optarg, "mmcblk", 6)) || (!strncmp(optarg, "sd", 2)) || (!strncmp(optarg, "boot", 4)))
					{
						my_printf("Flashing kernel with arg %s\n", optarg);
						strcpy(kernel_device_arg, optarg);
						user_kernel = 1;
					}
				}
				else
					my_printf("Flashing kernel\n");
				break;
			case 'r':
				flash_rootfs = 1;
				if (optarg)
				{
					if ((!strncmp(optarg, "mtd", 3)) || (!strncmp(optarg, "mmcblk", 6)) || (!strncmp(optarg, "sd", 2)) || (!strncmp(optarg, "dreambox-rootfs", 15)))
					{
						my_printf("Flashing rootfs with arg %s\n", optarg);
						strcpy(rootfs_device_arg, optarg);
						user_rootfs = 1;
					}
				}
				else
					my_printf("Flashing rootfs\n");
				break;
			case 'm':
				if (optarg)
				{
					errno = 0;
					val = strtol(optarg, &endptr, 10);
					if (errno != 0 || endptr == optarg)
					{
						my_printf("Error: Wrong multiboot partition value. Only numeric values are allowed!\n");
						show_help = 1;
						return 0;
					}
					else if (val > 0)
					{
						multiboot_partition = val;
						my_printf("Flashing multiboot partition %d\n", multiboot_partition);
					}
					else if (val == 0)
					{
						my_printf("Flashing without rootSubDir check \n");
						rootsubdir_check = 1;
					}
				}
				break;
			case 's':
				if (optarg) {
					my_printf("Using user defined slot directory: %s\n", optarg);
					strcpy(slotname, optarg);
					user_slotname = 1;
				}
				break;
			case 'n':
				no_write = 1;
				break;
			case 'f':
				force_e2_stop = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case '?':
				show_help = 1;
				return 0;
		}
	}

	if (argc == 1)
	{
		show_help = 1;
		return 0;
	}

	if (optind + 1 < argc)
	{
		my_printf("Wrong parameter: %s\n\n", argv[optind+1]);
		show_help = 1;
		return 0;
	}
	else if (optind + 1 == argc)
	{
		if (!find_image_files(argv[optind]))
			return 0;

		if (flash_kernel == 0 && flash_rootfs== 0) // set defaults
		{
			my_printf("Setting default parameter: Flashing kernel and rootfs\n");
			flash_kernel = 1;
			flash_rootfs = 1;
		}
	}
	else
	{
		my_printf("Error: Image_directory parameter missing!\n\n");
		show_help = 1;
		return 0;
	}

	return 1;
}

int read_mtd_file()
{
	FILE* f;

	f = fopen("/proc/mtd", "r");
	if (f == NULL)
	{ 
		perror("Error while opening /proc/mtd");
		// for testing try to open local mtd file
		f = fopen("./mtd", "r");
		if (f == NULL)
			return 0;
	}

	char line [1000];
	char dev  [1000];
	char size [1000];
	char esize[1000];
	char name [1000];
	char dev_path[] = "/dev/";
	int line_nr = 0;
	unsigned long devsize;
	int wrong_user_mtd = 0;

	my_printf("Found /proc/mtd entries:\n");
	my_printf("Device:   Size:     Erasesize:  Name:                   Image:\n");
	while (fgets(line, 1000, f) != NULL)
	{
		line_nr++;
		if (line_nr == 1) // check header
		{
			sscanf(line, "%s%s%s%s", dev, size, esize, name);
			if (strcmp(dev  , "dev:") != 0
			 || strcmp(size , "size") != 0
			 || strcmp(esize, "erasesize") != 0
			 || strcmp(name , "name") != 0)
			{
				my_printf("Error: /proc/mtd has an invalid format\n");
				fclose(f);
				return 0;
			}
		}
		else
		{
			sscanf(line, "%s%s%s%s", dev, size, esize, name);
			my_printf("%s %12s %9s    %-18s", dev, size, esize, name);
			devsize = strtoul(size, 0, 16);
			if (dev[strlen(dev)-1] == ':') // cut ':'
				dev[strlen(dev)-1] = '\0';
			// user selected kernel
			if (user_kernel && !strcmp(dev, kernel_device_arg))
			{
				strcpy(&kernel_device[0], dev_path);
				strcpy(&kernel_device[5], kernel_device_arg);
				if (kernel_file_stat.st_size <= devsize)
				{
					if ((strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0
						|| strcmp(name, "\"kernel2\"") == 0
						|| strcmp(name, "\"boot\"") == 0))
					{
						if (kernel_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", kernel_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_kernel_device = 1;
						kernel_flash_mode = MTD;
					}
					else
					{
						my_printf("  <-  Error: Selected by user is not a kernel mtd!!\n");
						wrong_user_mtd = 1;
					}
				}
				else
				{
					my_printf("  <-  Error: Kernel file is bigger than device size!!\n");
					wrong_user_mtd = 1;
				}
			}
			// user selected rootfs
			else if (user_rootfs && !strcmp(dev, rootfs_device_arg))
			{
				strcpy(&rootfs_device[0], dev_path);
				strcpy(&rootfs_device[5], rootfs_device_arg);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					if ((strcmp(name, "\"rootfs\"") == 0
						|| strcmp(name, "\"rootfs2\"") == 0
						|| strcmp(name, "\"dreambox-rootfs\"") == 0))
					{
						if (rootfs_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", rootfs_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_rootfs_device = 1;
						rootfs_flash_mode = MTD;
					}
					else
					{
						my_printf("  <-  Error: Selected by user is not a rootfs mtd!!\n");
						wrong_user_mtd = 1;
					}
				}
				else if (strcmp(esize, "0001f000") == 0)
				{
					my_printf("  <-  Error: Invalid erasesize\n");
					wrong_user_mtd = 1;
				}
				else
				{
					my_printf("  <-  Error: Rootfs file is bigger than device size!!\n");
					wrong_user_mtd = 1;
				}
			}
			// auto kernel
			else if (!user_kernel
					&& (strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0
						|| (strcmp(name, "\"boot\"") == 0 && multiboot_partition == -1)
						|| (strcmp(name, "\"linuxkernel1\"") == 0 && multiboot_partition == 1)
						|| (strcmp(name, "\"linuxkernel2\"") == 0 && multiboot_partition == 2)))
			{
				if (found_kernel_device)
				{
					my_printf("\n");
					continue;
				}
				strcpy(&kernel_device[0], dev_path);
				strcpy(&kernel_device[5], dev);
				if (kernel_file_stat.st_size <= devsize)
				{
					if (kernel_filename[0] != '\0')
						my_printf("  ->  %s\n", kernel_filename);
					else
						my_printf("\n");
					found_kernel_device = 1;
					kernel_flash_mode = MTD;
				}
				else
					my_printf("  <-  Error: Kernel file is bigger than device size!!\n");
			}
			// auto rootfs
			else if (!user_rootfs 
					&& (strcmp(name, "\"rootfs\"") == 0
						|| strcmp(name, "\"dreambox-rootfs\"") == 0
						|| strcmp(name, "\"root\"") == 0
						|| (strcmp(name, "\"userdata\"") == 0 && multiboot_partition != -1)))
			{
				if (found_rootfs_device)
				{
					my_printf("\n");
					continue;
				}
				strcpy(&rootfs_device[0], dev_path);
				strcpy(&rootfs_device[5], dev);
				unsigned long devsize;
				devsize = strtoul(size, 0, 16);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					if (rootfs_filename[0] != '\0')
						my_printf("  ->  %s\n", rootfs_filename);
					else
						my_printf("\n");
					found_rootfs_device = 1;
					if (strcmp(name, "\"userdata\"") == 0) // box with subdir feature in mtd partition e.g. sfx6008
					{
						rootfs_flash_mode = TARBZ2_MTD;
						sprintf(rootfs_sub_dir, "%s%d", slotname, multiboot_partition);
					}
					else
						rootfs_flash_mode = MTD;
				}
				else if (strcmp(esize, "0001f000") == 0)
					my_printf("  <-  Error: Invalid erasesize\n");
				else
					my_printf("  <-  Error: Rootfs file is bigger than device size!!\n");
			}
			else
				my_printf("\n");
		}
	}

	my_printf("Using kernel mtd device: %s\n", kernel_device);
	my_printf("Using rootfs mtd device: %s\n", rootfs_device);

	fclose(f);

	if (wrong_user_mtd)
	{
		my_printf("Error: User selected mtd device cannot be used!\n");
		return 0;
	}

	return 1;
}

int kernel_flash(char* device, char* filename)
{
	if (kernel_flash_mode == TARBZ2)
		return flash_ext4_kernel(device, filename, kernel_file_stat.st_size, quiet, no_write);
	else if (kernel_flash_mode == MTD)
		return flash_ubi_jffs2_kernel(device, filename, quiet, no_write);
}

int rootfs_flash(char* device, char* filename)
{
	if (rootfs_flash_mode == TARBZ2 || rootfs_flash_mode == TARBZ2_MTD)
	{
		my_printf("Flash rootfs unpack\n");
		return flash_unpack_rootfs(filename, quiet, no_write);
	}
	else if (rootfs_flash_mode == MTD)
	{
		if (rootfs_type == EXT4) // MTD rootfs with unknown format -> expect ubifs as only ubifs boxes support this
			rootfs_type = UBIFS;
		return flash_ubi_jffs2_rootfs(device, filename, rootfs_type, quiet, no_write);
	}
}

/* detect rootfs type
 * checks whether /newroot is mounted as tmpfs
 * find mountpoint on which the rootfs image files are located
 */
int readProcMounts()
{
	FILE *f;
	struct mntent *mountEntry;
	dev_t devno_of_name;
	int block_dev;
	int subdir_too = 1;
	struct stat dummy_stat;

	mountlist = NULL;
	rootfs_type = UNKNOWN;
	rootfs_mount_point[0] = '\0';

	if (rootfs_filename[0] != '\0') // rootfs image file found
	{
		devno_of_name = rootfs_file_stat.st_dev;
		block_dev = 0;
		if (S_ISBLK(rootfs_file_stat.st_mode) || S_ISCHR(rootfs_file_stat.st_mode))
		{
			devno_of_name = rootfs_file_stat.st_rdev;
			block_dev = 1;
		}
	}

	f = setmntent("/proc/mounts", "r");
	if (!f)
	{
		perror("Error while opening /proc/mounts");
		return 0;
	}

	while ((mountEntry = getmntent(f)) != NULL)
	{
		// detect rootfs type
		if ((strstr(mountEntry->mnt_fsname, "rootfs") != NULL || strstr(mountEntry->mnt_fsname, "ubifs") != NULL)
		 && strcmp(mountEntry->mnt_dir, "/") == 0
		 && strcmp(mountEntry->mnt_type, "ubifs") == 0)
		{
			my_printf("Found UBIFS rootfs\n");
			rootfs_type = UBIFS;
			strncpy(ubi_fs_name, mountEntry->mnt_fsname, 1000);
		}
		else if (strstr(mountEntry->mnt_fsname, "root") != NULL
			  && strcmp(mountEntry->mnt_dir, "/") == 0
			  && strcmp(mountEntry->mnt_type, "jffs2") == 0)
		{
			my_printf("Found JFFS2 rootfs\n");
			rootfs_type = JFFS2;
		}
		else if (strcmp(mountEntry->mnt_dir, "/") == 0
			  && strcmp(mountEntry->mnt_type, "ext4") == 0)
		{
			my_printf("Found EXT4 rootfs\n");
			rootfs_type = EXT4;
		}
		// check newroot
		else if (strcmp(mountEntry->mnt_dir, "/newroot") == 0
			  && strcmp(mountEntry->mnt_type, "tmpfs") == 0)
		{
			my_printf("Found mounted /newroot\n");
			newroot_mounted = 1;
		}
		else
		{
			if (rootfs_filename[0] != '\0')
			{
				// find mountpoint on which the image files are located
				if (strcmp(rootfs_filename, mountEntry->mnt_dir) == 0
				 || strcmp(rootfs_filename, mountEntry->mnt_fsname) == 0
				 || (stat(mountEntry->mnt_fsname, &dummy_stat) == 0 && dummy_stat.st_rdev == devno_of_name)
				 || (stat(mountEntry->mnt_dir, &dummy_stat) == 0 && dummy_stat.st_dev == devno_of_name))
				{
					strcpy(rootfs_mount_point, mountEntry->mnt_dir);
				}
				else // store all other mounts to unmount them
				{
					if (strcmp(mountEntry->mnt_dir, "/") != 0
					 && strcmp(mountEntry->mnt_dir, "/sys") != 0
					 && strcmp(mountEntry->mnt_dir, "/dev") != 0
					 && strcmp(mountEntry->mnt_dir, "/dev/pts") != 0
					 && strcmp(mountEntry->mnt_dir, "/proc") != 0
					 && strcmp(mountEntry->mnt_dir, "/var/volatile") != 0)
					{
						mountlist_entry = malloc(sizeof(*mountlist_entry));
						mountlist_entry->next = mountlist;
						mountlist_entry->dir = strdup(mountEntry->mnt_dir);
						mountlist = mountlist_entry;
					}
				}
			}
		}
	}
	endmntent(f);

	if (rootfs_type == UNKNOWN)
		my_printf("Found unknown rootfs\n");

	if (rootfs_mount_point[0] != '\0')
		my_printf("Found mountpoint for rootfs file: %s\n", rootfs_mount_point);

	return 1;
}

int exec_ps()
{
	// call ps
	optind = 0; // reset getopt_long
	char* argv[] = {
		"ps",		// program name
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Execute: ps\n");
	if (ps_main(argc, argv) == 9999)
	{
		return 1; // e2 found
	}
	return 0; // e2 not found
}

int check_e2_stopped()
{
	int time = 0;
	int max_time = 70;
	int e2_found = 1;

	set_step_progress(0);
	if (!quiet)
		my_printf("Checking E2 is running...\n");
	while (time < max_time && e2_found)
	{
		e2_found = exec_ps();

		if (!e2_found)
		{
			if (!quiet)
				my_printf("E2 is stopped\n");
		}
		else
		{
			sleep(2);
			time += 2;
			if (!quiet)
				my_printf("E2 still running\n");
		}
		set_step_progress(time * 100 / max_time);
	}

	if (e2_found)
		return 0;

	return 1;
}

int exec_fuser_kill()
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"fuser",		// program name
		"-k",			// kill
		"-m",			// mount point
		"/oldroot/",	// rootfs
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Execute: fuser -k -m /oldroot/\n");
	if (!no_write)
		if (fuser_main(argc, argv) != 0)
			return 0;

	return 1;
}

int daemonize()
{
	// Prevents that ofgwrite will be killed when init 1 is performed
	my_printf("daemonize\n");

	pid_t pid = fork();
	if (pid < 0)
	{
		my_printf("Error fork failed\n");
		return 0;
	}
	else if (pid > 0)
	{
		// stop parent
		exit(EXIT_SUCCESS);
	}

	if (setsid() < 0)
	{
		my_printf("Error setsid failed\n");
		return 0;
	}

	pid = fork();
	if (pid < 0)
	{
		my_printf("Error 2. fork failed\n");
		return 0;
	}
	else if (pid > 0)
	{
		// stop child
		exit(EXIT_SUCCESS);
	}

	umask(0);
	my_printf(" successful\n");
	return 1;
}

int umount_rootfs(int steps)
{
	DIR *dir;
	int multilib = 1;

	if ((dir = opendir("/lib64")) == NULL)
	{
		multilib = 0;
	}

	int ret = 0;
	my_printf("start umount_rootfs\n");
	// the start script creates /newroot dir and mount tmpfs on it
	// create directories
	ret += chdir("/newroot");
	ret += mkdir("/newroot/bin", 777);
	ret += mkdir("/newroot/dev", 777);
	ret += mkdir("/newroot/etc", 777);
	ret += mkdir("/newroot/dev/pts", 777);
	ret += mkdir("/newroot/lib", 777);
	ret += mkdir("/newroot/media", 777);
	ret += mkdir("/newroot/oldroot", 777);
	ret += mkdir("/newroot/oldroot_remount", 777);
	ret += mkdir("/newroot/proc", 777);
	ret += mkdir("/newroot/run", 777);
	ret += mkdir("/newroot/sbin", 777);
	ret += mkdir("/newroot/sys", 777);
	ret += mkdir("/newroot/usr", 777);
	ret += mkdir("/newroot/usr/lib", 777);
	ret += mkdir("/newroot/usr/lib/autofs", 777);
	ret += mkdir("/newroot/usr/sbin", 777);
	ret += mkdir("/newroot/var", 777);
	ret += mkdir("/newroot/var/volatile", 777);

	if (multilib)
	{
		ret += mkdir("/newroot/lib64", 777);
		ret += mkdir("/newroot/usr/lib64", 777);
		ret += mkdir("/newroot/usr/lib64/autofs", 777);
	}
	if (android)
	{
		ret += mkdir("/newroot/dreamcard", 777);
	}

	// create maybe needed directory for image files mountpoint
	char path[1000];
	snprintf(path, sizeof(path), "mkdir -p /newroot/%s", rootfs_mount_point);
	ret += system(path);

	if (ret != 0)
	{
		my_printf("Error creating necessary directories\n");
		return 0;
	}

	// we need init and libs to be able to exec init u later
	if (multilib)
	{
		ret =  system("cp -arf /bin/busybox*     /newroot/bin");
		ret += system("cp -arf /bin/sh*          /newroot/bin");
		ret += system("cp -arf /bin/bash*        /newroot/bin");
		ret += system("cp -arf /sbin/init*       /newroot/sbin");
		ret += system("cp -arf /lib64/libc*        /newroot/lib64");
		ret += system("cp -arf /lib64/ld*          /newroot/lib64");
		ret += system("cp -arf /lib64/libtinfo*    /newroot/lib64");
		ret += system("cp -arf /lib64/libdl*       /newroot/lib64");
		if (android)
		{
			ret += system("cp -arf /sbin/blkid*       /newroot/sbin");
			ret += system("cp -arf /lib64/libblkid.*   /newroot/lib64");
		}
	}
	else
	{
		ret =  system("cp -arf /bin/busybox*     /newroot/bin");
		ret += system("cp -arf /bin/sh*          /newroot/bin");
		ret += system("cp -arf /bin/bash*        /newroot/bin");
		ret += system("cp -arf /sbin/init*       /newroot/sbin");
		ret += system("cp -arf /lib/libc*        /newroot/lib");
		ret += system("cp -arf /lib/ld*          /newroot/lib");
		ret += system("cp -arf /lib/libtinfo*    /newroot/lib");
		ret += system("cp -arf /lib/libdl*       /newroot/lib");
		if (android)
		{
			ret += system("cp -arf /sbin/blkid*       /newroot/sbin");
			ret += system("cp -arf /lib/libblkid.*   /newroot/lib");
		}
	}

	if (ret != 0)
	{
		my_printf("Error copying binary and libs\n");
		return 0;
	}

	// libcrypt is moved from /lib to /usr/libX in new OE versions
	if (multilib)
	{
		ret = system("cp -arf /lib64/libcrypt*    /newroot/lib64");
		if (ret != 0)
		{
			ret = system("cp -arf /usr/lib64/libcrypt*    /newroot/usr/lib64");
			if (ret != 0)
			{
				my_printf("Error copying libcrypto lib\n");
				return 0;
			}
		}
	}
	else
	{
		ret = system("cp -arf /lib/libcrypt*    /newroot/lib");
		if (ret != 0)
		{
			ret = system("cp -arf /usr/lib/libcrypt*    /newroot/usr/lib");
			if (ret != 0)
			{
				my_printf("Error copying libcrypto lib\n");
				return 0;
			}
		}
	}

	// copy for automount ignore errors as autofs is maybe not installed
	if (multilib)
	{
		ret = system("cp -arf /usr/sbin/autom*  /newroot/bin");
		ret += system("cp -arf /etc/auto*        /newroot/etc");
		ret += system("cp -arf /lib64/libpthread*  /newroot/lib64");
		ret += system("cp -arf /lib64/libnss*      /newroot/lib64");
		ret += system("cp -arf /lib64/libnsl*      /newroot/lib64");
		ret += system("cp -arf /lib64/libresolv*   /newroot/lib64");
		ret += system("cp -arf /lib64/librt*       /newroot/lib64");
		ret += system("cp -arf /usr/lib64/libtirp* /newroot/usr/lib64");
		ret += system("cp -arf /usr/lib64/autofs/* /newroot/usr/lib64/autofs");
		ret += system("cp -arf /etc/nsswitch*    /newroot/etc");
		ret += system("cp -arf /etc/resolv*      /newroot/etc");
	}
	else
	{
		ret = system("cp -arf /usr/sbin/autom*  /newroot/bin");
		ret += system("cp -arf /etc/auto*        /newroot/etc");
		ret += system("cp -arf /lib/libpthread*  /newroot/lib");
		ret += system("cp -arf /lib/libnss*      /newroot/lib");
		ret += system("cp -arf /lib/libnsl*      /newroot/lib");
		ret += system("cp -arf /lib/libresolv*   /newroot/lib");
		ret += system("cp -arf /lib/librt*       /newroot/lib");
		ret += system("cp -arf /usr/lib/libtirp* /newroot/usr/lib");
		ret += system("cp -arf /usr/lib/autofs/* /newroot/usr/lib/autofs");
		ret += system("cp -arf /etc/nsswitch*    /newroot/etc");
		ret += system("cp -arf /etc/resolv*      /newroot/etc");
	}

	// Switch to user mode 1
	my_printf("Switching to user mode 2\n");
	ret = system("init 2");
	if (ret)
	{
		my_printf("Error switching runmode!\n");
		set_error_text("Error switching runmode! Abort flashing.");
		sleep(5);
		return 0;
	}

	// it can take several seconds until E2 is shut down
	// wait because otherwise remounting read only is not possible
	set_step("Wait until E2 is stopped");
	if (!check_e2_stopped())
	{
		my_printf("Error E2 can't be stopped! Abort flashing.\n");
		set_error_text("Error E2 can't be stopped! Abort flashing.");
		ret = system("init 3");
		return 0;
	}

	// some boxes don't allow to open framebuffer while e2 is running
	// reopen framebuffer to show the GUI
	close_framebuffer();
	init_framebuffer(steps);
	show_main_window(1, ofgwrite_version);
	set_overall_text("Flashing image");
	set_step_without_incr("Wait until E2 is stopped");
	sleep(2);

	ret = pivot_root("/newroot/", "oldroot");
	if (ret)
	{
		my_printf("Error executing pivot_root!\n");
		set_error_text("Error pivot_root! Abort flashing.");
		sleep(5);
		ret = system("init 3");
		return 0;
	}

	ret = chdir("/");
	// move mounts to new root
	ret =  mount("/oldroot/dev/", "dev/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/proc/", "proc/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/sys/", "sys/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/var/volatile", "var/volatile/", NULL, MS_MOVE, NULL);
	// create link for tmp
	ret += symlink("/var/volatile/tmp", "/tmp");
	if (ret != 0)
	{
		my_printf("Error move mounts to newroot\n");
		set_error_text1("Error move mounts to newroot. Abort flashing!");
		set_error_text2("Rebooting in 30 seconds!");
		sleep(30);
		reboot(LINUX_REBOOT_CMD_RESTART);
		return 0;
	}
	ret = mount("/oldroot/media/", "media/", NULL, MS_MOVE, NULL);  // ignore return value

	// move mount which includes the image files
	if ((strncmp(rootfs_mount_point, "/media/", 7) == 0 && ret != 0)
	 ||  strncmp(rootfs_mount_point, "/var/volatile/", 14) != 0)
	{
		my_printf("Move mountpoint of image files\n");
		char oldroot_path[1000];
		strcpy(oldroot_path, "/oldroot");
		strcat(oldroot_path, rootfs_mount_point);
		my_printf("Moving %s to %s\n", oldroot_path, rootfs_mount_point);
		// mount move: ignore errors as e.g. network shares cannot be moved
		mount(oldroot_path, rootfs_mount_point, NULL, MS_MOVE, NULL);
	}

	// umount all unneeded filesystems
	while (mountlist != NULL)
	{
		char oldroot_path[1000];
		my_printf("umounting: %s\n", mountlist->dir);
		strcpy(oldroot_path, "/oldroot");
		strcat(oldroot_path, mountlist->dir);
		umount2(oldroot_path, MNT_DETACH);
		free(mountlist->dir);
		mountlist = mountlist->next;
	}

	// create link for mount/umount for autofs
	ret = symlink("/bin/busybox", "/bin/mount");
	ret += symlink("/bin/busybox", "/bin/umount");

	// try to restart autofs
	ret =  system("/bin/automount");
	if (ret != 0)
	{
		my_printf("Error starting autofs\n");
	}

	// restart init process
	ret = system("exec init u");
	sleep(3);

	// kill all remaining open processes which prevent umounting rootfs
	ret = exec_fuser_kill();
	if (!ret)
		my_printf("fuser successful\n");
	sleep(3);

	ret = umount("/oldroot/newroot");
	ret = umount("/oldroot/");
	if (!ret)
		my_printf("umount successful\n");
	else
		my_printf("umount not successful\n");

	// mount oldroot to other mountpoint, because otherwise all data in not moved filesystems under /oldroot will be deleted
	if (rootfs_flash_mode == TARBZ2 || rootfs_flash_mode == TARBZ2_MTD)
	{
		if (rootfs_flash_mode == TARBZ2)
			ret = mount(rootfs_device, "/oldroot_remount/", "ext4", 0, NULL);
		else
			ret = mount(ubi_fs_name, "/oldroot_remount/", "ubifs", 0, NULL);
		if (!ret)
			my_printf("remount to /oldroot_remount/ successful\n");
		else
		{
			my_printf("Error remounting root! Abort flashing.\n");
			set_error_text1("Error remounting root! Abort flashing.");
			set_error_text2("Rebooting in 30 seconds");
			sleep(30);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return 0;
		}
	}
	else if (ret && rootfs_flash_mode != TARBZ2 && rootfs_flash_mode != TARBZ2_MTD) // umount failed -> remount read only
	{
		ret = mount("/oldroot/", "/oldroot/", "", MS_REMOUNT | MS_RDONLY, NULL);
		if (ret)
		{
			my_printf("Error remounting root ro! Abort flashing.\n");
			set_error_text1("Error remounting root ro! Abort flashing.");
			set_error_text2("Rebooting in 30 seconds");
			sleep(30);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return 0;
		}
	}

	return 1;
}

int check_env()
{
	if (!newroot_mounted)
	{
		my_printf("Please use ofgwrite command to start flashing!\n");
		return 0;
	}

	return 1;
}

void ext4_kernel_dev_found(const char* dev, int partition_number)
{
	found_kernel_device = 1;
	kernel_flash_mode = TARBZ2;
	sprintf(kernel_device, "%sp%d", dev, partition_number);
	my_printf("Using %s as kernel device\n", kernel_device);
}

void ext4_rootfs_dev_found(const char* dev, int partition_number)
{
	// Check whether rootfs is on the same device as current used rootfs
	sprintf(rootfs_device, "%sp", dev);
	if (strncmp(rootfs_device, current_rootfs_device, strlen(rootfs_device)) != 0)
	{
		my_printf("Rootfs(%s) is on different device than current rootfs(%s). Maybe wrong device selected. -> Aborting\n", dev, current_rootfs_device);
		return;
	}

	found_rootfs_device = 1;
	rootfs_flash_mode = TARBZ2;
	sprintf(rootfs_device, "%sp%d", dev, partition_number);
	my_printf("Using %s as rootfs device\n", rootfs_device);
}

void find_store_substring(char* src, char* cmp, char* dest)
{
	char* pos;
	char* pos2;

	if ((pos = strstr(src, cmp)) != NULL)
	{
		if (((pos2 = strstr(pos, " ")) != NULL) || ((pos2 = strstr(pos, "\n")) != NULL))
		{
			strncpy(dest, pos + strlen(cmp), pos2-pos-strlen(cmp));
			dest[pos2-pos-strlen(cmp)] = '\0';
		}
		else
		{
			strcpy(dest, pos + strlen(cmp));
		}
	}
}

/* Reads /proc/cmdline to distinguish whether current running image should be flashed.
 * It also tries to read block device partition table from cmdline.
 */
void readProcCmdline()
{
	my_printf("Read /proc/cmdline\n");
	FILE* f;

	f = fopen("/proc/cmdline", "r");
	if (f == NULL)
	{
		perror("Error while opening /proc/cmdline");
		return;
	}

	char line[4096];
	char* pos;
	memset(kexec_mode, 0, sizeof(kexec_mode));
	memset(current_rootfs_device, 0, sizeof(current_rootfs_device));
	memset(current_kernel_device, 0, sizeof(current_kernel_device));
	memset(current_rootfs_sub_dir, 0, sizeof(current_rootfs_sub_dir));

	if (fgets(line, 4096, f) != NULL)
	{
		find_store_substring(line, "kexec=", kexec_mode);
		find_store_substring(line, "root=", current_rootfs_device);
		find_store_substring(line, "kernel=", current_kernel_device);
		find_store_substring(line, "rootsubdir=", current_rootfs_sub_dir);
		my_printf("Kexec mode is: %s\n", kexec_mode);
		my_printf("Current rootfs is: %s\n", current_rootfs_device);
		my_printf("Current kernel is: %s\n", current_kernel_device);
		my_printf("Current root sub dir is: %s\n", current_rootfs_sub_dir);
		my_printf("\n");
		if ((pos = strstr(line, "blkdevparts=")) != NULL)
		{
			parse_cmdline_partition_table(pos + 12);
		}
	}
	fclose(f);
}

void find_kernel_rootfs_device()
{
	int mtd_kernel_found = found_kernel_device;
	// get kernel/rootfs from cmdline
	readProcCmdline();

	if ((!found_kernel_device || !found_rootfs_device) && strcmp(kexec_mode, "1") != 0) // Both kernel and rootfs needs to be found. Otherwise ignore found devices
	{
		found_kernel_device = 0;
		found_rootfs_device = 0;
		// get kernel/rootfs from fdisk
		// call fdisk -l
		optind = 0; // reset getopt_long
		char* argv[] = {
			"fdisk",		// program name
			"-l",			// list
			NULL
		};
		int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

		my_printf("Execute: fdisk -l\n");
		if (fdisk_main(argc, argv) != 0)
			return;
	}

	if (!found_kernel_device && mtd_kernel_found)
		found_kernel_device = 1;


	// force user kernel/rootfs
	if (user_rootfs)
	{
		if (current_rootfs_sub_dir[0] != '\0' && multiboot_partition == -1 && rootsubdir_check == 0) // box with rootSubDir feature
		{
			found_rootfs_device = 0;
			my_printf("Error: In case of rootSubDir multiboot with user defined rootfs -m parameter is mandatory\n", rootfs_device);
			return;
		}

		found_rootfs_device = 1;
		rootfs_flash_mode = TARBZ2;
		sprintf(rootfs_device, "/dev/%s", rootfs_device_arg);
		my_printf("Using %s as rootfs device\n", rootfs_device);
		if (current_rootfs_sub_dir[0] != '\0' && rootsubdir_check == 0)
		{
			sprintf(rootfs_sub_dir, "%s%d", slotname, multiboot_partition);
		}
	}
	if (user_kernel)
	{
		found_kernel_device = 1;
		kernel_flash_mode = TARBZ2;
		if (strcmp(kexec_mode, "1") != 0) {
			sprintf(kernel_device, "/dev/%s", kernel_device_arg);
		} else {
			sprintf(kernel_device, "/oldroot_remount/%s%d/%s", slotname, multiboot_partition, kernel_device_arg);
		}
		my_printf("Using %s as kernel device\n", kernel_device);
	}

	// use kexec kernel mode
	if (!found_kernel_device && strcmp(kexec_mode, "1") == 0)
	{
		found_kernel_device = 1;
		kernel_flash_mode = TARBZ2;
		sprintf(kernel_device, "/oldroot_remount/%s%d/zImage", slotname, multiboot_partition);
		my_printf("Using %s as kernel device\n", kernel_device);
	}

	// use kexec rootfs mode
	if (!found_rootfs_device && strcmp(kexec_mode, "1") == 0)
	{
		found_rootfs_device = 1;
		rootfs_flash_mode = TARBZ2;
		strcpy(rootfs_device, current_rootfs_device);
		my_printf("Using %s as rootfs device\n", rootfs_device);
		if (current_rootfs_sub_dir[0] != '\0' && rootsubdir_check == 0)
		{
			sprintf(rootfs_sub_dir, "%s%d", slotname, multiboot_partition);
		}
	}

	if  (((current_rootfs_sub_dir[0] == '\0' && strcmp(rootfs_device, current_rootfs_device) != 0 && rootfs_flash_mode != MTD) ||
		  ( current_rootfs_sub_dir[0] != '\0' && strcmp(current_rootfs_sub_dir, rootfs_sub_dir) != 0 )
		 ) && !force_e2_stop
		)
	{
		stop_e2_needed = 0;
		my_printf("Flashing currently not running image\n");
	}
}

// Checks whether kernel and rootfs device is bigger than the kernel and rootfs file
int check_device_size()
{
	unsigned long long devsize = 0;
	int fd = 0;
	// check kernel
	if (found_kernel_device && kernel_filename[0] != '\0' && kernel_flash_mode == TARBZ2 && (strcmp(kexec_mode, "1") != 0))
	{
		fd = open(kernel_device, O_RDONLY);
		if (fd <= 0)
		{
			my_printf("Unable to open kernel device %s. Aborting\n", kernel_device);
			return 0;
		}
		if (ioctl(fd, BLKGETSIZE64, &devsize))
		{
			my_printf("Couldn't determine kernel device size. Aborting\n");
			return 0;
		}
		if (kernel_file_stat.st_size > devsize)
		{
			my_printf("Kernel file(%lld) is bigger than kernel device(%llu). Aborting\n", kernel_file_stat.st_size, devsize);
			return 0;
		}
	}

	// check rootfs
	if (found_rootfs_device && rootfs_filename[0] != '\0' && rootfs_flash_mode == TARBZ2)
	{
		fd = open(rootfs_device, O_RDONLY);
		if (fd <= 0)
		{
			my_printf("Unable to open rootfs device %s. Aborting\n", rootfs_device);
			return 0;
		}
		if (ioctl(fd, BLKGETSIZE64, &devsize))
		{
			my_printf("Couldn't determine rootfs device size. Aborting\n");
			return 0;
		}
		if (rootfs_file_stat.st_size > devsize)
		{
			my_printf("Rootfs file (%lld) is bigger than rootfs device(%llu). Aborting\n", rootfs_file_stat.st_size, devsize);
			return 0;
		}
	}

	return 1;
}

void handle_busybox_fatal_error()
{
	my_printf("Error flashing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
	set_error_text1("Error untar rootfs. System won't boot!");
	set_error_text2("Please flash backup! Rebooting in 60 sec");
	if (stop_e2_needed)
	{
		sleep(60);
		reboot(LINUX_REBOOT_CMD_RESTART);
	}
	sleep(30);
	close_framebuffer();
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	// Check if running on a box or on a PC. Stop working on PC to prevent overwriting important files
#if defined(__i386) || defined(__x86_64__)
	my_printf("You're running ofgwrite on a PC. Aborting...\n");
	exit(EXIT_FAILURE);
#endif

	// Open log
	openlog("ofgwrite", LOG_CONS | LOG_NDELAY, LOG_USER);

	my_printf("\nofgwrite Utility v%s\n", ofgwrite_version);
	my_printf("Author: Betacentauri\n");
	my_printf("Based upon: mtd-utils-native-1.5.1 and busybox 1.24.1\n");
	my_printf("Use at your own risk! Make always a backup before use!\n");
	my_printf("Don't use it if you use multiple ubi volumes in ubi layer!\n\n");

	int ret;
	found_kernel_device = 0;
	found_rootfs_device = 0;
	kernel_flash_mode = FLASH_MODE_UNKNOWN;
	rootfs_flash_mode = FLASH_MODE_UNKNOWN;
	ubi_fs_name[0] = '\0';

	ret = read_args(argc, argv);

	if (!ret || show_help)
	{
		printUsage();
		return EXIT_FAILURE;
	}

	// set rootfs type and more
	if (!readProcMounts())
		return EXIT_FAILURE;

	// find kernel and rootfs devices
	my_printf("\n");
	read_mtd_file();
	find_kernel_rootfs_device();

	if (flash_kernel && (!found_kernel_device || kernel_filename[0] == '\0'))
	{
		my_printf("Error: Cannot flash kernel");
		if (!found_kernel_device)
			my_printf(", because no kernel device was found\n");
		else
			my_printf(", because no kernel file was found\n");
		return EXIT_FAILURE;
	}

	if (flash_rootfs && (!found_rootfs_device || rootfs_filename[0] == '\0' || rootfs_type == UNKNOWN))
	{
		my_printf("Error: Cannot flash rootfs");
		if (!found_rootfs_device)
			my_printf(", because no rootfs device was found\n");
		else if (rootfs_filename[0] == '\0')
			my_printf(", because no rootfs file was found\n");
		else
			my_printf(", because rootfs type is unknown\n");
		return EXIT_FAILURE;
	}

	if (!check_device_size())
		return EXIT_FAILURE;

	my_printf("\n");

	if (flash_kernel && !flash_rootfs) // flash only kernel
	{
		if (!quiet)
			my_printf("Flashing kernel ...\n");

		init_framebuffer(2);
		show_main_window(0, ofgwrite_version);
		set_overall_text("Flashing kernel");

		if (!kernel_flash(kernel_device, kernel_filename))
			ret = EXIT_FAILURE;
		else
			ret = EXIT_SUCCESS;

		if (!quiet && ret == EXIT_SUCCESS)
		{
			my_printf("done\n");
			set_step("Successfully flashed kernel!");
			sleep(5);
		}
		else if (ret == EXIT_FAILURE)
		{
			my_printf("failed. System won't boot. Please flash backup!\n");
			set_error_text1("Error flashing kernel. System won't boot!");
			set_error_text2("Please flash backup! Go back to E2 in 60 sec");
			sleep(60);
		}
		closelog();
		close_framebuffer();
		return ret;
	}

	if (flash_rootfs)
	{
		ret = 0;

		// Check whether /newroot exists and is mounted as tmpfs
		if (!check_env())
		{
			closelog();
			return EXIT_FAILURE;
		}

		int steps = 6;
		if (flash_kernel && rootfs_flash_mode != TARBZ2 && rootfs_flash_mode != TARBZ2_MTD)
			steps+= 2;
		else if (flash_kernel && (rootfs_flash_mode == TARBZ2 || rootfs_flash_mode == TARBZ2_MTD))
			steps+= 1;
		init_framebuffer(steps);
		show_main_window(0, ofgwrite_version);
		set_overall_text("Flashing image");
		set_step("Killing processes");

		// kill nmbd, smbd, rpc.mountd and rpc.statd -> otherwise remounting root read-only is not possible
		if (!no_write && stop_e2_needed)
		{
			ret = system("killall nmbd");
			ret = system("killall smbd");
			ret = system("killall rpc.mountd");
			ret = system("killall rpc.statd");
			ret = system("/etc/init.d/softcam stop");
			ret = system("killall CCcam");
			ret = system("pkill -9 -f '[Oo][Ss][Cc][Aa][Mm]'");
			ret = system("ps w | grep -i oscam | grep -v grep | awk '{print $1}'| xargs kill -9");
			ret = system("pkill -9 -f '[Ww][Ii][Cc][Aa][Rr][Dd][Dd]'");
			ret = system("ps w | grep -i wicardd | grep -v grep | awk '{print $1}'| xargs kill -9");
			ret = system("killall kodi.bin");
			ret = system("killall hddtemp");
			ret = system("killall transmission-daemon");
			ret = system("killall openvpn");
			ret = system("/etc/init.d/sabnzbd stop");
			ret = system("pkill -9 -f cihelper");
			ret = system("pkill -9 -f ciplus_helper");
			ret = system("pkill -9 -f ciplushelper");
			// kill VMC
			ret = system("pkill -f vmc.sh");
			ret = system("pkill -f DBServer.py");
			// stop autofs
			ret = system("/etc/init.d/autofs stop");
			// ignore return values, because the processes might not run
		}

		// sync filesystem
		my_printf("Syncing filesystem\n");
		set_step("Syncing filesystem");
		sync();
		sleep(1);

		set_step("init 2");
		if (!no_write && stop_e2_needed)
		{
			if (!daemonize())
			{
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
			if (!umount_rootfs(steps))
			{
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}
		// if not running rootfs is flashed then we need to mount it before start flashing
		if (!no_write && !stop_e2_needed && (rootfs_flash_mode == TARBZ2 || rootfs_flash_mode == TARBZ2_MTD))
		{
			set_step("Mount rootfs");
			my_printf("Mount rootfs\n");
			mkdir("/oldroot_remount", 777);
			// mount rootfs device
			if (rootfs_flash_mode == TARBZ2_MTD) // box with mtd subdir feature e.g. sfx6008
				ret = mount(ubi_fs_name, "/oldroot_remount/", "ubifs", 0, NULL);
			else
				ret = mount(rootfs_device, "/oldroot_remount/", "ext4", 0, NULL);
			if (!ret)
				my_printf("Mount to /oldroot_remount/ successful\n");
			else if (errno == EINVAL && rootfs_flash_mode != TARBZ2_MTD)
			{
				// most likely partition is not formatted -> format it
				char mkfs_cmd[100];
				sprintf(mkfs_cmd, "mkfs.ext4 -F %s", rootfs_device);
				my_printf("Formatting %s\n", rootfs_device);
				ret = system(mkfs_cmd);
				if (!ret)
				{ // try to mount it again
					ret = mount(rootfs_device, "/oldroot_remount/", "ext4", 0, NULL);
					if (!ret)
						my_printf("Mount to /oldroot_remount/ successful\n");
				}
			}
			if (ret)
			{
				my_printf("Error remounting root! Abort flashing.\n");
				set_error_text1("Error mounting root! Abort flashing.");
				sleep(3);
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}

		if (!no_write) {
			char tmp[1016];
			sprintf(tmp, "/oldroot_remount/%s", rootfs_sub_dir);
			my_printf("Creating directory %s recursively\n", rootfs_sub_dir);
			bb_make_directory(tmp, -1, FILEUTILS_RECUR);
		}

		// Flash rootfs
		if (!rootfs_flash(rootfs_device, rootfs_filename))
		{
			my_printf("Error flashing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
			set_error_text1("Error flashing rootfs. System won't boot!");
			set_error_text2("Please flash backup! Rebooting in 60 sec");
			if (stop_e2_needed)
			{
				sleep(60);
				reboot(LINUX_REBOOT_CMD_RESTART);
			}
			sleep(3);
			close_framebuffer();
			return EXIT_FAILURE;
		}
		my_printf("Successfully flashed rootfs!\n");

		//Flash kernel
		if (flash_kernel)
		{
			if (!quiet)
				my_printf("Flashing kernel ...\n");

			if (!kernel_flash(kernel_device, kernel_filename))
			{
				my_printf("Error flashing kernel. System won't boot. Please flash backup! Starting E2 in 60 seconds\n");
				set_error_text1("Error flashing kernel. System won't boot!");
				set_error_text2("Please flash backup! Starting E2 in 60 sec");
				if (stop_e2_needed)
				{
					sleep(60);
					reboot(LINUX_REBOOT_CMD_RESTART);
				}
				sleep(3);
				close_framebuffer();
				return EXIT_FAILURE;
			}
			sync();
			my_printf("Successfully flashed kernel!\n");
		}

		//Android boot kernel.img Dreambox one/two
		if (android)
		{
			set_step("Create Kernel.img");
			const char *dreamcard_device = "/dev/mmcblk1p1";
			const char *dreamcard_mount = "/dreamcard";
			char device_root[256];
			char label[50] = "";
			char *position = strchr(rootfs_device, 'p');
			int length = position - rootfs_device;
			strncpy(device_root, rootfs_device, length);
			device_root[length] = '\0';

			if (strcmp(device_root, "/dev/mmcblk1") == 0)
			{
				my_printf("Mount dreamcard\n");
				mkdir(dreamcard_mount, 777);

				FILE *device = fopen("/dev/mmcblk1p1", "rb");
				if (device == NULL) {
					my_printf("Error opening dreamcard device");
					dreamcard = 0;
				}

				char command[100];
				snprintf(command, sizeof(command), "blkid.util-linux -s LABEL -o value %s", dreamcard_device);
				FILE *fp = popen(command, "r");
				char label[50];
				fgets(label, sizeof(label), fp);
				pclose(fp);

				if (strcmp(label, "\n") == 0) {
					my_printf("Info: no label is present\n");
					dreamcard = 0;
				}

				if (strcmp(label, "DREAMCARD\n") != 0) {
					my_printf("Info: The device label does not match 'dreamcard'\n");
					dreamcard = 0;
				}
				if (dreamcard) {
					ret = umount(dreamcard_device);
					sync();
					sleep(1);
					if (mount(dreamcard_device, dreamcard_mount, "vfat", 0, NULL) == -1) {
						my_printf("Error: dreamcard device '%s' not mounted to '%s':%s.\n",dreamcard_device, dreamcard_mount, strerror(errno));
						rmdir(dreamcard_mount);
						return EXIT_FAILURE;
					}
					size_t length = strlen(rootfs_device);
					int kernelnr = (length > 0) ? rootfs_device[length - 1] - '0' : -1;
					if (kernelnr < 0 || kernelnr > 9) {
						my_printf("Error: Invalid kernel number\n");
						rmdir(dreamcard_mount);
						return EXIT_FAILURE;
					}
					char filename[50];
					snprintf(filename, sizeof(filename), "/dreamcard/kernel%d.img", kernelnr);
					my_printf("start generate %s image on %s\n", filename, dreamcard_device);
					generate_boot_image(filename);
					sync();
					sleep(1);
					ret = umount2(dreamcard_mount, MNT_DETACH);
					ret = rmdir(dreamcard_mount);
				}
			}
			my_printf("start generate /oldroot_remount/boot/kernel.img image on device %s\n", rootfs_device);
			generate_boot_image("/oldroot_remount/boot/kernel.img");
			sync();
			sleep(1);
		}

		sync();
		sleep(1);
		if (!stop_e2_needed)
		{
			ret = umount2("/oldroot_remount/", MNT_DETACH);
			ret = rmdir("/oldroot_remount/");
			ret = umount2("/newroot/", MNT_DETACH);
			ret = rmdir("/newroot/");
			my_printf("Successfully flashed image\n");
			set_step("Successfully flashed!");
		}
		else
		{
			ret = umount("/oldroot_remount/");
			my_printf("Successfully flashed image Rebooting in 3 seconds...\n");
			set_step("Successfully flashed! Rebooting in 3 seconds");
		}
		fflush(stdout);
		fflush(stderr);
		sleep(3);
		if (!no_write && stop_e2_needed)
		{
			reboot(LINUX_REBOOT_CMD_RESTART);
		}
	}

	closelog();
	close_framebuffer();

	return EXIT_SUCCESS;
}
