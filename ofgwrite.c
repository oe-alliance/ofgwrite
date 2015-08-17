#include <stdio.h>
#include <stdarg.h>
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
#include <libmtd.h>
#include <errno.h>
#include <mtd/mtd-abi.h>


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
enum RootfsTypeEnum
{
	UNKNOWN, UBIFS, JFFS2
} rootfs_type;


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
		path[strlen(path)+1] = '0';
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
			 || strcmp(entry->d_name, "oe_kernel.bin") == 0	// DAGS boxes
			 || strcmp(entry->d_name, "uImage") == 0)	// Spark boxes
			{
				strcpy(kernel_filename, path);
				strcpy(&kernel_filename[strlen(path)], entry->d_name);
				stat(kernel_filename, &kernel_file_stat);
				my_printf("Found kernel file: %s\n", kernel_filename);
			}
			if (strcmp(entry->d_name, "rootfs.bin") == 0			// ET-xx00, XP1000
			 || strcmp(entry->d_name, "root_cfe_auto.bin") == 0		// Solo2
			 || strcmp(entry->d_name, "root_cfe_auto.jffs2") == 0	// other VU boxes
			 || strcmp(entry->d_name, "oe_rootfs.bin") == 0	// DAGS boxes
			 || strcmp(entry->d_name, "e2jffs2.img") == 0)	// Spark boxes
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
				strcpy(&kernel_mtd_device[0], dev_path);
				strcpy(&kernel_mtd_device[5], kernel_mtd_device_arg);
				if (kernel_file_stat.st_size <= devsize)
				{
					if ((strcmp(name, "\"kernel\"") == 0
						|| strcmp(name, "\"nkernel\"") == 0))
					{
						if (kernel_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", kernel_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_mtd_kernel = 1;
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
				strcpy(&rootfs_mtd_device[0], dev_path);
				strcpy(&rootfs_mtd_device[5], rootfs_mtd_device_arg);
				if (rootfs_file_stat.st_size <= devsize
					&& strcmp(esize, "0001f000") != 0)
				{
					if (strcmp(name, "\"rootfs\"") == 0)
					{
						if (rootfs_filename[0] != '\0')
							my_printf("  ->  %s <- User selected!!\n", rootfs_filename);
						else
							my_printf("  <-  User selected!!\n");
						found_mtd_rootfs = 1;
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
				if (found_mtd_kernel)
				{
					my_printf("\n");
					continue;
				}
				strcpy(&kernel_mtd_device[0], dev_path);
				strcpy(&kernel_mtd_device[5], dev);
				if (kernel_file_stat.st_size <= devsize)
				{
					if (kernel_filename[0] != '\0')
						my_printf("  ->  %s\n", kernel_filename);
					else
						my_printf("\n");
					found_mtd_kernel = 1;
				}
				else
					my_printf("  <-  Error: Kernel file is bigger than device size!!\n");
			}
			// auto rootfs
			else if (!user_mtd_rootfs && strcmp(name, "\"rootfs\"") == 0)
			{
				if (found_mtd_rootfs)
				{
					my_printf("\n");
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
						my_printf("  ->  %s\n", rootfs_filename);
					else
						my_printf("\n");
					found_mtd_rootfs = 1;
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

	my_printf("Using kernel mtd device: %s\n", kernel_mtd_device);
	my_printf("Using rootfs mtd device: %s\n", rootfs_mtd_device);

	fclose(f);

	if (wrong_user_mtd)
	{
		my_printf("Error: User selected mtd device cannot be used!\n");
		return 0;
	}

	return 1;
}

int getFlashType(char* device)
{
	libmtd_t libmtd = libmtd_open();
	if (libmtd == NULL)
	{
		if (errno == 0)
			my_printf("MTD is not present in the system");
		my_printf("cannot open libmtd %s", strerror(errno));
		return -1;
	}

	struct mtd_dev_info mtd;
	int err = mtd_get_dev_info(libmtd, device, &mtd);
	if (err)
	{
		my_fprintf(stderr, "cannot get information about \"%s\"\n", device);
		return -1;
	}

	libmtd_close(libmtd);

	return mtd.type;
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
		my_printf("Erasing %s: flash_erase %s 0 0\n", context, device);
	if (!no_write)
		if (flash_erase_main(argc, argv) != 0)
			return 0;

	return 1;
}

int flash_erase_jffs2(char* device, char* context)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"flash_erase",	// program name
		"-j",			// format the device for jffs2
		device,			// device
		"0",			// start offset
		"0",			// block count
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		my_printf("Erasing %s: flash_erase -j %s 0 0\n", context, device);
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
		my_printf("Flashing kernel: nandwrite %s %s %s\n", opts, device, filename);
	if (!no_write)
		if (nandwrite_main(argc, argv) != 0)
			return 0;

	return 1;
}

int kernel_flash(char* device, char* filename)
{
	int type = getFlashType(device);
	if (type == -1)
		return 0;

	if (type == MTD_NANDFLASH || type == MTD_MLCNANDFLASH)
	{
		my_printf("Found NAND flash\n");
		// Erase
		set_step("Erasing kernel");
		if (!flash_erase(kernel_mtd_device, "kernel"))
		{
			my_printf("Error erasing kernel! System might not boot. If you have problems please flash backup!\n");
			return 0;
		}

		// Flash
		set_step("Writing kernel");
		if (!flash_write(kernel_mtd_device, kernel_filename))
		{
			my_printf("Error flashing kernel! System won't boot. Please flash backup!\n");
			return 0;
		}
	}
	else if (type == MTD_NORFLASH)
	{
		my_printf("Found NOR flash\n");
		if (!flashcp(kernel_mtd_device, kernel_filename, 0))
		{
			my_printf("Error flashing kernel! System won't boot. Please flash backup!\n");
			return 0;
		}
	}
	else
	{
		my_fprintf(stderr, "Flash type \"%d\" not supported\n", type);
		return 0;
	}

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

	my_printf("Flashing rootfs: ubiformat %s -f %s\n", device, filename);
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

	my_printf("Flashing rootfs: ubiupdatevol %s %s\n", ubivol_device, filename);
	if (!no_write)
		if (ubiupdatevol_main(argc, argv) != 0)
			return 0;

	return 1;
}

int flashcp(char* device, char* filename, int reboot)
{
	optind = 0; // reset getopt_long
	char opts[4];
	if (reboot)
		strcpy(opts, "-vr\0");
	else
		strcpy(opts, "-v\0");
	char* argv[] = {
		"flashcp",		// program name
		opts,			// options -v verbose -r reboot immediately after flashing
		filename,		// file to flash
		device,			// device
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Flashing rootfs: flashcp %s %s %s\n", opts, filename, device);
	if (!no_write)
		if (flashcp_main(argc, argv) != 0)
			return 0;

	return 1;
}

int rootfs_flash(char* device, char* filename)
{
	int type = getFlashType(device);
	if (type == -1)
		return 0;

	if ((type == MTD_NANDFLASH || type == MTD_MLCNANDFLASH) && rootfs_type == UBIFS)
	{
		my_printf("Found NAND flash\n");
		if (!ubi_write(device, filename))
			return 0;
	}
	else if ((type == MTD_NANDFLASH || type == MTD_MLCNANDFLASH) && rootfs_type == JFFS2)
	{
		my_printf("Found NAND flash\n");
		if (!flash_erase_jffs2(device, "rootfs"))
			return 0;
		if (!flash_write(device, filename))
			return 0;
	}
	else if (type == MTD_NORFLASH && rootfs_type == JFFS2)
	{
		my_printf("Found NOR flash\n");
		if (!flashcp(device, filename, 1))
			return 0;
	}
	else
	{
		my_fprintf(stderr, "Flash type \"%d\" in combination with rootfs type %d is not supported\n", type, rootfs_type);
		return 0;
	}

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
	my_printf("Rootfs: Ubi device number: %d, Volume id: %d, Device: %s\n", dev_num, vol_info.vol_id, rootfs_ubi_device);

	libubi_close(libubi);
	return 1;
}

void setRootfsType()
{
	FILE* f;

	f = fopen("/proc/mounts", "r");
	if (f == NULL)
	{ 
		perror("Error while opening /proc/mounts");
		rootfs_type = UNKNOWN;
		return;
	}

	char line [1000];
	while (fgets(line, 1000, f) != NULL)
	{
		if (strstr (line, "rootfs") != NULL &&
			strstr (line, "ubifs") != NULL)
		{
			my_printf("Found UBIFS\n");
			rootfs_type = UBIFS;
			return;
		}
		else if (strstr (line, "root") != NULL &&
				 strstr (line, "jffs2") != NULL)
		{
			my_printf("Found JFFS2\n");
			rootfs_type = JFFS2;
			return;
		}
	}

	my_printf("Found unknown rootfs\n");
	rootfs_type = UNKNOWN;
}

int check_e2_stopped()
{
	FILE *fp;
	int nbytes = 100;
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
	if (e2_found)
		return 0;
	return 1;
}

int main(int argc, char *argv[])
{
	// Open log
	openlog("ofgwrite", LOG_CONS | LOG_NDELAY, LOG_USER);

	my_printf("\nofgwrite Utility v2.2.3\n");
	my_printf("Author: Betacentauri\n");
	my_printf("Based upon: mtd-utils-native-1.5.1\n");
	my_printf("Use at your own risk! Make always a backup before use!\n");
	my_printf("Don't use it if you use multiple ubi volumes in ubi layer!\n\n");

	int ret;

	ret = read_args(argc, argv);

	if (!ret || show_help)
	{
		printUsage();
		return EXIT_FAILURE;
	}

	found_mtd_kernel = 0;
	found_mtd_rootfs = 0;

	my_printf("\n");
	if (!read_mtd_file())
		return EXIT_FAILURE;

	my_printf("\n");

	setRootfsType();

	my_printf("\n");

	if (flash_kernel && (!found_mtd_kernel || kernel_filename[0] == '\0'))
	{
		my_printf("Error: Cannot flash kernel");
		if (!found_mtd_kernel)
			my_printf(", because no kernel MTD entry was found\n");
		else
			my_printf(", because no kernel file was found\n");
		return EXIT_FAILURE;
	}

	if (flash_rootfs && (!found_mtd_rootfs || rootfs_filename[0] == '\0' || rootfs_type == UNKNOWN))
	{
		my_printf("Error: Cannot flash rootfs");
		if (!found_mtd_rootfs)
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

		init_framebuffer(2);
		set_overall_text("Flashing kernel");

		if (!kernel_flash(kernel_mtd_device, kernel_filename))
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

		// Switch to user mode 2
		my_printf("Switching to user mode 2\n");
		if (!no_write)
		{
			ret = system("init 2");
			if (ret)
			{
				my_printf("Error switching mode!\n");
				return EXIT_FAILURE;
			}
		}

		int steps = 6;
		if (flash_kernel)
			steps+= 2;
		init_framebuffer(steps);
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
			ret = system("killall -9 oscam");
			ret = system("killall -9 oscam_oscamupdater");
			ret = system("killall -9 oscam_emu");
			ret = system("killall hddtemp");
			ret = system("killall transmission-daemon");
			ret = system("killall openvpn");
			ret = system("/etc/init.d/sabnzbd stop");
			// ignore return values, because the processes might not run
		}

		// it can take several seconds until E2 is shut down
		// wait because otherwise remounting read only is not possible
		set_step("Wait until E2 is stopped");
		if (!no_write)
		{
			if (!check_e2_stopped())
			{
				my_printf("Error E2 can't be stopped! Abort flashing.\n");
				set_error_text("Error E2 can't be stopped! Abort flashing.");
				system("init 3");
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}
		// Remove rest of E2 osd
		clearOSD();
		// reload background image because if E2 was running when init_framebuffer was executed the image is not visible
		loadBackgroundImage();

		// sync filesystem
		my_printf("Syncing filesystem\n");
		set_step("Syncing filesystem");
		ret = system("sync");
		if (ret)
		{
			my_printf("Error syncing filesystem! Rebooting in 60 seconds\n");
			set_error_text1("Error syncing filesystem!");
			set_error_text2("Rebooting in 60 seconds");
			sleep(60);
			reboot(LINUX_REBOOT_CMD_RESTART);
			return EXIT_FAILURE;
		}

		sleep(1);

		// Remount root read-only
		my_printf("Remounting rootfs read-only\n");
		set_step("Remounting rootfs read-only");
		if (!no_write)
		{
			ret = system("mount -r -o remount /");
			if (ret)
			{
				my_printf("Error remounting root! Abort flashing. Try to restart E2 in 30 seconds\n");
				set_error_text1("Error remounting root! Abort flashing.");
				set_error_text2("Try to restart E2 in 30 seconds");
				sleep(30);
				system("init 3");
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
		}

		// sync again (most likely unnecessary)
		ret = system("sync");

		//Flash kernel
		if (flash_kernel)
		{
			if (!quiet)
				my_printf("Flashing kernel ...\n");

			if (!kernel_flash(kernel_mtd_device, kernel_filename))
			{
				my_printf("Error flashing kernel. System won't boot. Please flash backup! Starting E2 in 60 seconds\n");
				set_error_text1("Error flashing kernel. System won't boot!");
				set_error_text2("Please flash backup! Starting E2 in 60 sec");
				sleep(60);
				system("init 3");
				closelog();
				close_framebuffer();
				return EXIT_FAILURE;
			}
			my_printf("Successfully flashed kernel!\n");
		}

		// Flash rootfs
		if (!rootfs_flash(rootfs_mtd_device, rootfs_filename))
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
