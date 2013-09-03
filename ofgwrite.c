#include "ofgwrite.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <getopt.h>
#include <linux/reboot.h>
#include <libubi.h>
#include <syslog.h>
#include <sys/stat.h>
#include <unistd.h>


int flash_kernel = 0;
int flash_rootfs = 0;
int no_write     = 0;
int quiet        = 0;
int show_help    = 0;
int found_mtd_kernel;
int found_mtd_rootfs;
int user_mtd_kernel = 0;
int user_mtd_rootfs = 0;
char kernel_filename[1000];
char kernel_mtd_device[1000];
char kernel_mtd_device_arg[1000];
char rootfs_filename[1000];
char rootfs_mtd_device[1000];
char rootfs_mtd_device_arg[1000];
char rootfs_ubi_device[1000];
int rootfs_mtd_num = -1;
struct stat kernel_file_stat;
struct stat rootfs_file_stat;


void printUsage()
{
	printf("Usage: ofgwrite <parameter> <image_directory>\n");
	printf("Options:\n");
	printf("   -k --kernel           flash kernel with automatic device recognition(default)\n");
	printf("   -kmtdx --kernel=mtdx  use mtdx device for kernel flashing\n");
	printf("   -r --rootfs           flash rootfs with automatic device recognition(default)\n");
	printf("   -rmtdy --rootfs=mtdy  use mtdy device for rootfs flashing\n");
	printf("   -n --nowrite          show only found image and mtd partitions (no write)\n");
	printf("   -q --quiet            show less output\n");
	printf("   -h --help             show help\n");
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
		path[strlen(path)+1] = '0';
	}

	d = opendir(path);

	if (!d)
	{
		perror("Error reading image_directory");
		printf("\n");
		return 0;
	}

	do
	{
		entry = readdir(d);
		if (entry)
		{
			if (strcmp(entry->d_name, "kernel.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "kernel_cfe_auto.bin") == 0)	// VU boxes
			{
				strcpy(kernel_filename, path);
				strcpy(&kernel_filename[strlen(path)], entry->d_name);
				stat(kernel_filename, &kernel_file_stat);
				printf("Found kernel file: %s\n", kernel_filename);
			}
			if (strcmp(entry->d_name, "rootfs.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "root_cfe_auto.bin") == 0		// Solo2
			 || strcmp(entry->d_name, "root_cfe_auto.jffs2") == 0)	// other VU boxes
			{
				strcpy(rootfs_filename, path);
				strcpy(&rootfs_filename[strlen(path)], entry->d_name);
				stat(rootfs_filename, &rootfs_file_stat);
				printf("Found rootfs file: %s\n", rootfs_filename);
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
						printf("Flashing kernel with arg %s\n", optarg);
						strcpy(kernel_mtd_device_arg, optarg);
						user_mtd_kernel = 1;
					}
				}
				else
					printf("Flashing kernel\n");
				break;
			case 'r':
				flash_rootfs = 1;
				if (optarg)
				{
					if (!strncmp(optarg, "mtd", 3))
					{
						printf("Flashing rootfs with arg %s\n", optarg);
						strcpy(rootfs_mtd_device_arg, optarg);
						user_mtd_rootfs = 1;
					}
				}
				else
					printf("Flashing rootfs\n");
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
		printf("Wrong parameter: %s\n\n", argv[optind+1]);
		show_help = 1;
		return 0;
	}
	else if (optind + 1 == argc)
	{
		printf("Searching image files in %s\n", argv[optind]);
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
		printf("Error: Image_directory parameter missing!\n\n");
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

	printf("Found /proc/mtd entries:\n");
	printf("Device:   Size:     Erasesize:  Name:                   Image:\n");
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
				printf("Error: /proc/mtd has an invalid format\n");
				return 0;
			}
		}
		else
		{
			sscanf(line, "%s%s%s%s", dev, size, esize, name);
			printf("%s %12s %9s    %-18s", dev, size, esize, name);
			devsize = strtoul(size, 0, 16);
			if (dev[strlen(dev)-1] == ':') // cut ':'
				dev[strlen(dev)-1] = '\0';
			// user selected kernel
			if (user_mtd_kernel && !strcmp(dev, kernel_mtd_device_arg))
			{
				strcpy(&kernel_mtd_device[0], dev_path);
				strcpy(&kernel_mtd_device[5], kernel_mtd_device_arg);
				if (kernel_file_stat.st_size <= devsize)
				{
					if ((strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0))
					{
						if (kernel_filename[0] != '\0')
							printf("  ->  %s <- User selected!!\n", kernel_filename);
						else
							printf("  <-  User selected!!\n");
						found_mtd_kernel = 1;
					}
					else
					{
						printf("  <-  Error: Selected by user is not a kernel mtd!!\n");
						wrong_user_mtd = 1;
					}
				}
				else
				{
					printf("  <-  Error: Kernel file is bigger than device size!!\n");
					wrong_user_mtd = 1;
				}
			}
			// user selected rootfs
			else if (user_mtd_rootfs && !strcmp(dev, rootfs_mtd_device_arg))
			{
				strcpy(&rootfs_mtd_device[0], dev_path);
				strcpy(&rootfs_mtd_device[5], rootfs_mtd_device_arg);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					if (strcmp(name, "\"rootfs\"") == 0)
					{
						if (rootfs_filename[0] != '\0')
							printf("  ->  %s <- User selected!!\n", rootfs_filename);
						else
							printf("  <-  User selected!!\n");
						found_mtd_rootfs = 1;
					}
					else
					{
						printf("  <-  Error: Selected by user is not a rootfs mtd!!\n");
						wrong_user_mtd = 1;
					}
				}
				else if (strcmp(esize, "0001f000") == 0)
				{
					printf("  <-  Error: Invalid erasesize\n");
					wrong_user_mtd = 1;
				}
				else
				{
					printf("  <-  Error: Rootfs file is bigger than device size!!\n");
					wrong_user_mtd = 1;
				}
			}
			// auto kernel
			else if (!user_mtd_kernel
					&& (strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0))
			{
				if (found_mtd_kernel)
				{
					printf("\n");
					continue;
				}
				strcpy(&kernel_mtd_device[0], dev_path);
				strcpy(&kernel_mtd_device[5], dev);
				if (kernel_file_stat.st_size <= devsize)
				{
					if (kernel_filename[0] != '\0')
						printf("  ->  %s\n", kernel_filename);
					else
						printf("\n");
					found_mtd_kernel = 1;
				}
				else
					printf("  <-  Error: Kernel file is bigger than device size!!\n");
			}
			// auto rootfs
			else if (!user_mtd_rootfs && strcmp(name, "\"rootfs\"") == 0)
			{
				if (found_mtd_rootfs)
				{
					printf("\n");
					continue;
				}
				strcpy(&rootfs_mtd_device[0], dev_path);
				strcpy(&rootfs_mtd_device[5], dev);
				unsigned long devsize;
				devsize = strtoul(size, 0, 16);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					rootfs_mtd_num = atoi(&dev[strlen(dev)-1]);
					if (rootfs_filename[0] != '\0')
						printf("  ->  %s\n", rootfs_filename);
					else
						printf("\n");
					found_mtd_rootfs = 1;
				}
				else if (strcmp(esize, "0001f000") == 0)
					printf("  <-  Error: Invalid erasesize\n");
				else
					printf("  <-  Error: Rootfs file is bigger than device size!!\n");
			}
			else
				printf("\n");
		}
	}

	printf("Using kernel mtd device: %s\n", kernel_mtd_device);
	printf("Using rootfs mtd device: %s\n", rootfs_mtd_device);

	fclose(f);

	if (wrong_user_mtd)
	{
		printf("Error: User selected mtd device cannot be used!\n");
		return 0;
	}

	return 1;
}

int flash_erase(char* device, char* context)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"flash_erase",	// program name
		device,			// device
		"0",			// start offset
		"0",			// block count
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		printf("Erasing %s: flash_erase %s 0 0\n", context, device);
	if (!no_write)
		if (flash_erase_main(argc, argv) != 0)
			return 0;

	return 1;
}

int flash_write(char* device, char* filename)
{
	optind = 0; // reset getopt_long
	char opts[4];
	strcpy(opts, "-pm");
	char* argv[] = {
		"nandwrite",	// program name
		opts,			// options -p for pad and -m for mark bad blocks
		device,			// device
		filename,		// file to flash
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		printf("Flashing kernel: nandwrite %s %s %s\n", opts, device, filename);
	if (!no_write)
		if (nandwrite_main(argc, argv) != 0)
			return 0;

	return 1;
}

int ubi_write(char* device, char* filename)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"ubiformat",	// program name
		device,			// device
		"-f",			// flash file
		filename,		// file to flash
		"-D",			// no detach check
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	printf("Flashing rootfs: ubiformat %s -f %s\n", device, filename);
	if (!no_write)
		if (ubiformat_main(argc, argv) != 0)
			return 0;

	return 1;
}

int ubi_write_volume(char* ubivol_device, char* filename)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"ubiupdatevol",	// program name
		ubivol_device,	// device
		filename,		// file to flash
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	printf("Flashing rootfs: ubiupdatevol %s %s\n", ubivol_device, filename);
	if (!no_write)
		if (ubiupdatevol_main(argc, argv) != 0)
			return 0;

	return 1;
}

int setUbiDeviveName(int mtd_num, char* volume_name)
{
	libubi_t libubi;
	struct ubi_vol_info vol_info;
	int dev_num = -1;

	libubi = libubi_open();
	if (!libubi)
		return 0;

	if (mtd_num2ubi_dev(libubi, mtd_num, &dev_num))
	{
		libubi_close(libubi);
		return 0;
	}

	if (ubi_get_vol_info1_nm(libubi, dev_num, volume_name, &vol_info))
	{
		libubi_close(libubi);
		return 0;
	}

	sprintf(rootfs_ubi_device, "/dev/ubi%d_%d", dev_num, vol_info.vol_id);
	printf("Rootfs: Ubi device number: %d, Volume id: %d, Device: %s\n", dev_num, vol_info.vol_id, rootfs_ubi_device);

	libubi_close(libubi);
	return 1;
}

int main(int argc, char *argv[])
{
	printf("\nofgwrite Utility v1.4\n");
	printf("Author: Betacentauri\n");
	printf("Based upon: mtd-utils-native-1.4.9\n");
	printf("Use at your own risk! Make always a backup before use!\n");
	printf("Don't use it if you use multiple ubi volumes in ubi layer!\n\n");

	int ret;

	openlog("ofgwrite", LOG_CONS | LOG_NDELAY, LOG_USER);
	syslog(LOG_INFO, "Program start");

	ret = read_args(argc, argv);
	syslog(LOG_INFO, "After read_args");

	if (!ret || show_help)
	{
		printUsage();
		return -1;
	}

	found_mtd_kernel = 0;
	found_mtd_rootfs = 0;

	printf("\n");
	
	if (!read_mtd_file())
		return -1;
	syslog(LOG_INFO, "After read_mtd_file");

	printf("\n");

	if (flash_kernel && (!found_mtd_kernel || kernel_filename[0] == '\0'))
	{
		printf("Error: Cannot flash kernel");
		if (!found_mtd_kernel)
			printf(", because no kernel MTD entry was found\n");
		else
			printf(", because no kernel file was found\n");
		return -1;
	}

	if (flash_rootfs && (!found_mtd_rootfs || rootfs_filename[0] == '\0'))
	{
		printf("Error: Cannot flash rootfs");
		if (!found_mtd_rootfs)
			printf(", because no rootfs MTD entry was found\n");
		else
			printf(", because no rootfs file was found\n");
		return -1;
	}

	if (flash_kernel)
	{
		if (quiet)
			printf("Flashing kernel ...");
		syslog(LOG_INFO, "Flash kernel -> flash_erase %s", kernel_mtd_device);
		// Erase
		if (!flash_erase(kernel_mtd_device, "kernel"))
		{
			printf("Error erasing kernel! System might not boot. If you have problems please flash backup!\n");
			syslog(LOG_INFO, "Error flash_erase");
			return -1;
		}

		syslog(LOG_INFO, "Flash kernel -> flash_write device %s file %s", kernel_mtd_device, kernel_filename);
		// Flash
		if (!flash_write(kernel_mtd_device, kernel_filename))
		{
			printf("Error flashing kernel! System won't boot. Please flash backup!\n");
			syslog(LOG_INFO, "Error flash_write");
			return -1;
		}
		if (quiet)
			printf("done\n");
		syslog(LOG_INFO, "Flash kernel -> successful");
	}

	if (flash_rootfs)
	{
		ret = 0;

		// Switch to user mode 2
		printf("Switching to user mode 2\n");
		if (!no_write)
		{
			ret = system("init 2");
			if (ret)
			{
				printf("Error switching mode!\n");
				syslog(LOG_INFO, "Error: can't switch to user mode 2");
				return -1;
			}
		}
		syslog(LOG_INFO, "switched to user mode 2");
		sleep(1);

		// kill nmbd and smbd -> otherwise remounting root read-only is not possible
		if (!no_write)
		{
			ret = system("killall nmbd");
			ret = system("killall smbd");
			// ignore return values, because the processes might not run
		}

		sleep(4);

		// sync filesystem
		printf("Syncing filesystem\n");
		ret = system("sync");
		if (ret)
		{
			printf("Error syncing filesystem!\n");
			syslog(LOG_INFO, "Error: Syncing fs");
			return -1;
		}

		sleep(3);

		// Remount root read-only
		printf("Remounting root read-only\n");
		if (!no_write)
		{
			ret = system("mount -r -o remount /");
			if (ret)
			{
				printf("Error remounting root!\n");
				syslog(LOG_INFO, "Error remounting root read only");
				return -1;
			}
		}

		// sync again (most likely unnecessary)
		ret = system("sync");
		sleep(2);

		// Flash rootfs
		syslog(LOG_INFO, "Flash rootfs device %s file %s", rootfs_mtd_device, rootfs_filename);
		if (!ubi_write(rootfs_mtd_device, rootfs_filename))
		{
			printf("Error flashing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
			syslog(LOG_INFO, "Error: Flashing rootfs");
			sleep(60);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return -1;
		}

		printf("Successfully flashed rootfs! Rebooting in 5 seconds...\n");
		syslog(LOG_INFO, "Successfully flashed");
		sleep(5);
		if (!no_write)
		{
			syslog(LOG_INFO, "Rebooting");
			reboot(LINUX_REBOOT_CMD_RESTART);
		}
	}

	closelog();

	return 0;
}
