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
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>

const char ofgwrite_version[] = "3.1.1";
int flash_kernel = 0;
int flash_rootfs = 0;
int no_write     = 0;
int quiet        = 0;
int show_help    = 0;
int found_kernel_device = 0;
int found_rootfs_device = 0;
int user_mtd_kernel = 0;
int user_mtd_rootfs = 0;
int newroot_mounted = 0;
char kernel_filename[1000];
char kernel_device[1000];
char kernel_mtd_device_arg[1000];
char rootfs_filename[1000];
char rootfs_device[1000];
char rootfs_mtd_device_arg[1000];
char rootfs_ubi_device[1000];
enum RootfsTypeEnum rootfs_type;


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
	my_printf("   -k --kernel           flash kernel with automatic device recognition(default)\n");
	my_printf("   -kmtdx --kernel=mtdx  use mtdx device for kernel flashing\n");
	my_printf("   -r --rootfs           flash rootfs with automatic device recognition(default)\n");
	my_printf("   -rmtdy --rootfs=mtdy  use mtdy device for rootfs flashing\n");
	my_printf("   -n --nowrite          show only found image and mtd partitions (no write)\n");
	my_printf("   -q --quiet            show less output\n");
	my_printf("   -h --help             show help\n");
}

int find_image_files(char* p)
{
	DIR *d;
	struct dirent *entry;
	char path[1000];

	strcpy(path, p);
	kernel_filename[0] = '\0';
	rootfs_filename[0] = '\0';

	// add / to the end of the path
	if (path[strlen(path)-1] != '/')
	{
		path[strlen(path)] = '/';
		path[strlen(path)+1] = '\0';
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
			if (strcmp(entry->d_name, "kernel.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "kernel_cfe_auto.bin") == 0	// VU boxes
			 || strcmp(entry->d_name, "oe_kernel.bin") == 0			// DAGS boxes
			 || strcmp(entry->d_name, "uImage") == 0				// Spark boxes
			 || strcmp(entry->d_name, "kernel_auto.bin") == 0)		// solo4k
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
			 || strcmp(entry->d_name, "rootfs.tar.bz2") == 0)		// solo4k
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
	static const char *short_options = "k::r::nqh";
	static const struct option long_options[] = {
												{"kernel" , optional_argument, NULL, 'k'},
												{"rootfs" , optional_argument, NULL, 'r'},
												{"nowrite", no_argument      , NULL, 'n'},
												{"quiet"  , no_argument      , NULL, 'q'},
												{"help"   , no_argument      , NULL, 'h'},
												{NULL     , no_argument      , NULL,  0} };

	while ((opt= getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
	{
		switch (opt)
		{
			case 'k':
				flash_kernel = 1;
				if (optarg)
				{
					if (!strncmp(optarg, "mtd", 3))
					{
						my_printf("Flashing kernel with arg %s\n", optarg);
						strcpy(kernel_mtd_device_arg, optarg);
						user_mtd_kernel = 1;
					}
				}
				else
					my_printf("Flashing kernel\n");
				break;
			case 'r':
				flash_rootfs = 1;
				if (optarg)
				{
					if (!strncmp(optarg, "mtd", 3))
					{
						my_printf("Flashing rootfs with arg %s\n", optarg);
						strcpy(rootfs_mtd_device_arg, optarg);
						user_mtd_rootfs = 1;
					}
				}
				else
					my_printf("Flashing rootfs\n");
				break;
			case 'n':
				no_write = 1;
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
		my_printf("Searching image files in %s\n", argv[optind]);
		if (!find_image_files(argv[optind]))
			return 0;

		if (optind == 1) // only image dir was specified -> set defaults
		{
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
			if (user_mtd_kernel && !strcmp(dev, kernel_mtd_device_arg))
			{
				strcpy(&kernel_device[0], dev_path);
				strcpy(&kernel_device[5], kernel_mtd_device_arg);
				if (kernel_file_stat.st_size <= devsize)
				{
					if ((strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0
						|| strcmp(name, "\"kernel2\"") == 0))
					{
						if (kernel_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", kernel_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_kernel_device = 1;
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
			else if (user_mtd_rootfs && !strcmp(dev, rootfs_mtd_device_arg))
			{
				strcpy(&rootfs_device[0], dev_path);
				strcpy(&rootfs_device[5], rootfs_mtd_device_arg);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					if (strcmp(name, "\"rootfs\"") == 0
						|| strcmp(name, "\"rootfs2\"") == 0)
					{
						if (rootfs_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", rootfs_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_rootfs_device = 1;
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
			else if (!user_mtd_kernel
					&& (strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0))
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
				}
				else
					my_printf("  <-  Error: Kernel file is bigger than device size!!\n");
			}
			// auto rootfs
			else if (!user_mtd_rootfs && strcmp(name, "\"rootfs\"") == 0)
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
	if (rootfs_type == EXT4)
		return flash_ext4_kernel(device, filename, kernel_file_stat.st_size, quiet, no_write);
	else
		return flash_ubi_jffs2_kernel(device, filename, quiet, no_write);
}

int rootfs_flash(char* device, char* filename)
{
	if (rootfs_type == EXT4)
		return flash_ext4_rootfs(filename, quiet, no_write);
	else
		return flash_ubi_jffs2_rootfs(device, filename, rootfs_type, quiet, no_write);
}

// read root filesystem and checks whether /newroot is mounted as tmpfs
void readMounts()
{
	FILE* f;

	rootfs_type = UNKNOWN;

	f = fopen("/proc/mounts", "r");
	if (f == NULL)
	{ 
		perror("Error while opening /proc/mounts");
		return;
	}

	char line [1000];
	while (fgets(line, 1000, f) != NULL)
	{
		if (strstr (line, " / ") != NULL &&
			strstr (line, "rootfs") != NULL &&
			strstr (line, "ubifs") != NULL)
		{
			my_printf("Found UBIFS rootfs\n");
			rootfs_type = UBIFS;
		}
		else if (strstr (line, " / ") != NULL &&
				 strstr (line, "root") != NULL &&
				 strstr (line, "jffs2") != NULL)
		{
			my_printf("Found JFFS2 rootfs\n");
			rootfs_type = JFFS2;
		}
		else if (strstr (line, " / ") != NULL &&
				 strstr (line, "ext4") != NULL)
		{
			my_printf("Found EXT4 rootfs\n");
			rootfs_type = EXT4;
		}
		else if (strstr (line, "/newroot") != NULL &&
				 strstr (line, "tmpfs") != NULL)
		{
			my_printf("Found mounted /newroot\n");
			newroot_mounted = 1;
		}
	}

	fclose(f);

	if (rootfs_type == UNKNOWN)
		my_printf("Found unknown rootfs\n");
}

int check_e2_stopped()
{
	FILE *fp;
	size_t nbytes = 100;
	char* ps_line = (char*)malloc(nbytes + 1);
	int time = 0;
	int max_time = 60;
	int e2_found = 1;

	set_step_progress(0);
	if (!quiet)
		my_printf("Checking E2 is running...\n");
	while (time < max_time && e2_found)
	{
		fp = popen("busybox ps | grep /usr/bin/enigma2 | grep -v grep | grep -v enigma2.sh", "r");
		if (fp == NULL)
		{
			my_printf("Error ps cannot be executed!\n");
			free(ps_line);
			return 0;
		}

		if (getline(&ps_line, &nbytes, fp) == -1)
		{
			e2_found = 0;
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
		pclose(fp);
	}

	free(ps_line);

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

	set_info_text("Progress might stop now. Please wait until box reboots");
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

int umount_rootfs()
{
	int ret = 0;
	my_printf("start umount_rootfs\n");
	// the start script creates /newroot dir and mount tmpfs on it
	// create directories
	ret += chdir("/newroot");
	ret += mkdir("/newroot/bin", 777);
	ret += mkdir("/newroot/dev", 777);
	ret += mkdir("/newroot/dev/pts", 777);
	ret += mkdir("/newroot/lib", 777);
	ret += mkdir("/newroot/media", 777);
	ret += mkdir("/newroot/oldroot", 777);
	ret += mkdir("/newroot/proc", 777);
	ret += mkdir("/newroot/sbin", 777);
	ret += mkdir("/newroot/sys", 777);
	ret += mkdir("/newroot/var", 777);
	ret += mkdir("/newroot/var/volatile", 777);
	if (ret != 0)
	{
		my_printf("Error creating necessary directories\n");
		return 0;
	}

	// we need init and libs to be able to exec init u later
	ret =  system("cp -arf /bin/busybox*  /newroot/bin");
	ret += system("cp -arf /bin/sh*       /newroot/bin");
	ret += system("cp -arf /bin/bash*     /newroot/bin");
	ret += system("cp -arf /sbin/init*    /newroot/sbin");
	ret += system("cp -arf /lib/libcrypt* /newroot/lib");
	ret += system("cp -arf /lib/libc*     /newroot/lib");
	ret += system("cp -arf /lib/ld-*      /newroot/lib");
	if (ret != 0)
	{
		my_printf("Error copying binary and libs\n");
		return 0;
	}

	// Switch to user mode 1
	my_printf("Switching to user mode 1\n");
	ret = system("init 1");
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
	sleep(3);

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
	ret += mount("/oldroot/media/", "media/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/proc/", "proc/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/sys/", "sys/", NULL, MS_MOVE, NULL);
	ret += mount("/oldroot/var/volatile", "var/volatile/", NULL, MS_MOVE, NULL);
	if (ret != 0)
	{
		my_printf("Error move mounts to newroot\n");
		set_error_text1("Error move mounts to newroot. Abort flashing!");
		set_error_text2("Rebooting in 30 seconds!");
		sleep(30);
		reboot(LINUX_REBOOT_CMD_RESTART);
		return 0;
	}

	// restart init process
	ret = system("exec init u");
	sleep(3);

	// kill all remaining open processes which prevent umounting rootfs
	//ret = exec_fuser_kill();
	//if (!ret)
	//	my_printf("fuser successful\n");
	//sleep(3);

	ret = umount("/oldroot/");
	if (!ret)
		my_printf("umount successful\n");
	if (!ret && rootfs_type == EXT4) // umount success and ext4 -> remount again
	{
		ret = mount(rootfs_device, "/oldroot/", "ext4", 0, NULL);
		if (!ret)
			my_printf("remount successful\n");
	}
	else if (ret && rootfs_type != EXT4) // umount failed -> remount read only
	{
		ret = mount("/oldroot/", "/oldroot/", "", MS_REMOUNT | MS_RDONLY, NULL);
		if (ret)
		{
			my_printf("Error remounting root! Abort flashing.\n");
			set_error_text1("Error remounting root! Abort flashing.");
			set_error_text2("Rebooting in 30 seconds");
			sleep(30);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return 0;
		}
	}
	// else umount not successful and ext4 -> nevertheless try to exchange rootfs (normally it works)

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

int find_kernel_device()
{
	FILE *fp;
	size_t n = 500;
	char* line = (char*)malloc(n + 1);

	fp = popen("blkid -s PARTLABEL", "r");
	if (fp == NULL)
	{
		my_printf("Error blkid cannot be executed!\n");
		free(line);
		return 0;
	}

	while (getline(&line, &n, fp) != -1)
	{
		if (strstr(line, "PARTLABEL=\"kernel\"") != NULL)
		{
			char* pos = strstr(line, ":");
			strncpy(kernel_device, line, pos-line);
			if (kernel_device[0] != '\0')
			{
				found_kernel_device = 1;
				my_printf("Using %s as kernel device\n", kernel_device);
			}
		}
	}
	free(line);
	pclose(fp);

	if (!found_kernel_device)
	{
		my_printf("Error: No kernel device found!\n");
		return 0;
	}
	return 1;
}

int find_rootfs_device()
{
	FILE *fp;
	size_t n = 500;
	char* line = (char*)malloc(n + 1);

	fp = popen("blkid -s PARTLABEL", "r");
	if (fp == NULL)
	{
		my_printf("Error blkid cannot be executed!\n");
		free(line);
		return 0;
	}

	while (getline(&line, &n, fp) != -1)
	{
		if (strstr(line, "PARTLABEL=\"rootfs\"") != NULL)
		{
			char* pos = strstr(line, ":");
			strncpy(rootfs_device, line, pos-line);
			if (rootfs_device[0] != '\0')
			{
				found_rootfs_device = 1;
				my_printf("Using %s as rootfs device\n", rootfs_device);
			}
		}
	}
	free(line);
	pclose(fp);

	if (!found_rootfs_device)
	{
		my_printf("Error: No rootfs device found!\n");
		return 0;
	}
	return 1;
}

// Checks whether kernel and rootfs device is bigger than the kernel and rootfs file
int check_device_size()
{
	unsigned long long devsize = 0;
	int fd = 0;
	// check kernel
	if (found_kernel_device && kernel_filename[0] != '\0')
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
	if (found_rootfs_device && rootfs_filename[0] != '\0')
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

	ret = read_args(argc, argv);

	if (!ret || show_help)
	{
		printUsage();
		return EXIT_FAILURE;
	}

	// set rootfs type and more
	readMounts();

	if (rootfs_type == UBIFS || rootfs_type == JFFS2)
	{
		my_printf("\n");
		if (!read_mtd_file())
			return EXIT_FAILURE;
	}
	else if (rootfs_type == EXT4)
	{
		my_printf("\n");
		if (flash_kernel && !find_kernel_device())
			return EXIT_FAILURE;
		if (flash_rootfs && !find_rootfs_device())
			return EXIT_FAILURE;
		if (!check_device_size())
			return EXIT_FAILURE;
	}

	my_printf("\n");

	if (flash_kernel && (!found_kernel_device || kernel_filename[0] == '\0'))
	{
		my_printf("Error: Cannot flash kernel");
		if (!found_kernel_device)
			my_printf(", because no kernel MTD entry was found\n");
		else
			my_printf(", because no kernel file was found\n");
		return EXIT_FAILURE;
	}

	if (flash_rootfs && (!found_rootfs_device || rootfs_filename[0] == '\0' || rootfs_type == UNKNOWN))
	{
		my_printf("Error: Cannot flash rootfs");
		if (!found_rootfs_device)
			my_printf(", because no rootfs MTD entry was found\n");
		else if (rootfs_filename[0] == '\0')
			my_printf(", because no rootfs file was found\n");
		else
			my_printf(", because rootfs type is unknown\n");
		return EXIT_FAILURE;
	}

	if (flash_kernel && !flash_rootfs) // flash only kernel
	{
		if (!quiet)
			my_printf("Flashing kernel ...\n");

		init_framebuffer(2, ofgwrite_version);
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
		if (flash_kernel && rootfs_type != EXT4)
			steps+= 2;
		else if (flash_kernel && rootfs_type == EXT4)
			steps+= 1;
		init_framebuffer(steps, ofgwrite_version);
		set_overall_text("Flashing image");
		set_step("Killing processes");

		// kill nmbd, smbd, rpc.mountd and rpc.statd -> otherwise remounting root read-only is not possible
		if (!no_write)
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
			// ignore return values, because the processes might not run
		}

		// sync filesystem
		my_printf("Syncing filesystem\n");
		set_step("Syncing filesystem");
		sync();
		sleep(1);

		set_step("init 1");
		if (!no_write)
		{
			if (!daemonize())
			{
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
			if (!umount_rootfs())
			{
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}

		// Remove rest of E2 osd
		clearOSD();
		// reload background image because if E2 was running when init_framebuffer was executed the image is not visible
		loadBackgroundImage();

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
				sleep(60);
				reboot(LINUX_REBOOT_CMD_RESTART);
				return EXIT_FAILURE;
			}
			sync();
			my_printf("Successfully flashed kernel!\n");
		}

		// Flash rootfs
		if (!rootfs_flash(rootfs_device, rootfs_filename))
		{
			my_printf("Error flashing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
			set_error_text1("Error flashing rootfs. System won't boot!");
			set_error_text2("Please flash backup! Rebooting in 60 sec");
			sleep(60);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return EXIT_FAILURE;
		}

		my_printf("Successfully flashed rootfs! Rebooting in 3 seconds...\n");
		set_step("Successfully flashed! Rebooting in 3 seconds");
		fflush(stdout);
		fflush(stderr);
		sleep(3);
		if (!no_write)
		{
			reboot(LINUX_REBOOT_CMD_RESTART);
		}
	}

	closelog();
	close_framebuffer();

	return EXIT_SUCCESS;
}
