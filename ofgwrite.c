#include "ofgwrite.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <getopt.h>
#include <linux/reboot.h>
#include <libubi.h>


int flash_kernel = 0;
int flash_rootfs = 0;
int no_write     = 0;
int quiet        = 0;
int show_help    = 0;
int found_mtd_kernel;
int found_mtd_rootfs;
char kernel_filename[1000];
char kernel_mtd_device[1000];
char rootfs_filename[1000];
char rootfs_mtd_device[1000];
char rootfs_ubi_device[1000];
int rootfs_mtd_num = -1;


void printUsage()
{
	printf("Usage: ofgwrite <parameter> <image_directory>\n");
	printf("Options:\n");
	printf("   -k --kernel  flash kernel (default)\n");
	printf("   -r --rootfs  flash root (default)\n");
	printf("   -n --nowrite show only found image and mtd partitions (no write)\n");
	printf("   -q --quiet   show less output\n");
	printf("   -h --help    show help\n");
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
				printf("Found kernel file: %s\n", kernel_filename);
			}
			if (strcmp(entry->d_name, "rootfs.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "root_cfe_auto.bin") == 0		// Solo2
			 || strcmp(entry->d_name, "root_cfe_auto.jffs2") == 0)	// other VU boxes
			{
				strcpy(rootfs_filename, path);
				strcpy(&rootfs_filename[strlen(path)], entry->d_name);
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
	static const char *short_options = "krnqh";
	static const struct option long_options[] = {
												{"kernel" , no_argument, NULL, 'k'},
												{"rootfs" , no_argument, NULL, 'r'},
												{"nowrite", no_argument, NULL, 'n'},
												{"quiet"  , no_argument, NULL, 'q'},
												{"help"   , no_argument, NULL, 'h'},
												{NULL     , 0          , NULL,  0} };

	while ((opt= getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
	{
		switch (opt)
		{
			case 'k':
				flash_kernel = 1;
				break;
			case 'r':
				flash_rootfs = 1;
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
		printf("image_directory parameter missing\n\n");
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
		return 0;
	}

	char line [1000];
	char dev  [1000];
	char size [1000];
	char esize[1000];
	char name [1000];
	char dev_path[] = "/dev/";
	int line_nr = 0;

	printf("Found /proc/mtd entries:\n");
	printf("Device:   Size:     Erasesize:  Name:         Image:\n");
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
			if (strcmp(name, "\"kernel\"") == 0)
			{
				if (dev[strlen(dev)-1] == ':') // cut ':'
					dev[strlen(dev)-1] = '\0';
				strcpy(&kernel_mtd_device[0], dev_path);
				strcpy(&kernel_mtd_device[5], dev);
				if (kernel_filename[0] != '\0')
					printf("%s %8s %9s %11s  ->  %s\n", kernel_mtd_device, size, esize, name, kernel_filename);
				else
					printf("%s %8s %9s %11s\n", kernel_mtd_device, size, esize, name);
				found_mtd_kernel = 1;
			}
			if (strcmp(name, "\"rootfs\"") == 0)
			{
				if (dev[strlen(dev)-1] == ':') // cut ':'
					dev[strlen(dev)-1] = '\0';
				rootfs_mtd_num = atoi(&dev[strlen(dev)-1]);
				strcpy(&rootfs_mtd_device[0], dev_path);
				strcpy(&rootfs_mtd_device[5], dev);
				if (rootfs_filename[0] != '\0')
					printf("%s %8s %9s %11s  ->  %s\n", rootfs_mtd_device, size, esize, name, rootfs_filename);
				else
					printf("%s %8s %9s %11s\n", rootfs_mtd_device, size, esize, name);
				found_mtd_rootfs = 1;
			}
		}
	}

	fclose(f);
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
	printf("\nofgwrite Utility v0.4\n");
	printf("Author: Betacentauri\n");
	printf("Based upon: mtd-utils-native-1.4.9\n");
	printf("Use at your own risk! Make always a backup before use!\n");
	printf("Don't use it if you use multiple ubi volumes in ubi layer!\n\n");

	int ret;

	ret = read_args(argc, argv);

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

	printf("\n");

	//if (!setUbiDeviveName(rootfs_mtd_num, "rootfs"))
	//	return -1;

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
		// Erase
		if (!flash_erase(kernel_mtd_device, "kernel"))
		{
			printf("Error erasing kernel! System might not boot. If you have problems please flash backup!\n");
			return -1;
		}

		// Flash
		if (!flash_write(kernel_mtd_device, kernel_filename))
		{
			printf("Error flashing kernel! System won't boot. Please flash backup!\n");
			return -1;
		}
		if (quiet)
			printf("done\n");
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
				return -1;
			}
		}

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
				return -1;
			}
		}

		// sync again (most likely unnecessary)
		ret = system("sync");
		sleep(2);

		// Erase
		/*if (!flash_erase(rootfs_mtd_device, "rootfs"))
		{
			printf("Error erasing rootfs! System might not boot. If you have problems please flash backup! System will reboot in 60 seconds\n");
			sleep(60);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return -1;
		}*/

		// Flash rootfs
		if (!ubi_write(rootfs_mtd_device, rootfs_filename))
		{
			printf("Error flashing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
			sleep(60);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return -1;
		}
		/*if (!ubi_write_volume(rootfs_ubi_device, rootfs_filename))
		{
			printf("Error writing rootfs! System won't boot. Please flash backup! System will reboot in 60 seconds\n");
			sleep(60);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return -1;
		}*/

		printf("Successfully flashed rootfs! Rebooting in 5 seconds...\n");
		sleep(5);
		if (!no_write)
			reboot(LINUX_REBOOT_CMD_RESTART);
	}

	return 0;
}
