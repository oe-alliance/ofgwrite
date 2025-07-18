#include "ofgwrite.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <libubi.h>
#include <syslog.h>
#include <unistd.h>
#include <libmtd.h>
#include <errno.h>
#include <mtd/mtd-abi.h>
#include <sys/mount.h>
#include <dirent.h>


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

int flash_erase(char* device, char* context, int quiet, int no_write)
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

int flash_erase_jffs2(char* device, char* context, int quiet, int no_write)
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

int flash_write(char* device, char* filename, int quiet, int no_write)
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

int ubi_write(char* device, char* filename, int quiet, int no_write)
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

int ubi_detach_dev(unsigned int mtd_device, int quiet, int no_write)
{
	optind = 0; // reset getopt_long
	char device[100];

	snprintf(device, sizeof(device), "/dev/mtd%u", mtd_device);

	char* argv[] = {
		"ubidetach",	// program name
		"-p",			// path to device
		device,			// device
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Detach rootfs: ubidetach -p %s\n", device);
	if (!no_write)
		if (ubidetach_main(argc, argv) != 0)
			return 0;

	return 1;
}

int ubi_attach_ofgwrite(unsigned int mtd_device, unsigned int vid_offset, int quiet, int no_write)
{
	optind = 0; // reset getopt_long
	char mtd_device_str[100];
	char vid_offset_str[100];

	snprintf(mtd_device_str, sizeof(mtd_device_str), "%u", mtd_device);
	snprintf(vid_offset_str, sizeof(vid_offset_str), "%u", vid_offset);

	char* argv[] = {
		"ubiattach",	// program name
		"-d",			// ubi device number: use high hopefully unused number
		"10",
		"-m",			// mtd device number
		mtd_device_str,
		"-O",			// VID header offset
		vid_offset_str,
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Attach ubi: ubiattach -d 10 -m %u -O %u\n", mtd_device, vid_offset);
	if (!no_write)
		if (ubiattach_main(argc, argv) != 0)
			return 0;

	return 1;
}

int flashcp(char* device, char* filename, int reboot, int quiet, int no_write)
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

int flash_ubi_jffs2_kernel(char* device, char* filename, int quiet, int no_write)
{
	int type = getFlashType(device);
	if (type == -1)
		return 0;

	if (type == MTD_NANDFLASH || type == MTD_MLCNANDFLASH)
	{
		my_printf("Found NAND flash\n");
		// Erase
		set_step("Erasing kernel");
		if (!flash_erase(device, "kernel", quiet, no_write))
		{
			my_printf("Error erasing kernel! System might not boot. If you have problems please flash backup!\n");
			return 0;
		}

		// Flash
		set_step("Writing kernel");
		if (!flash_write(device, filename, quiet, no_write))
		{
			my_printf("Error flashing kernel! System won't boot. Please flash backup!\n");
			return 0;
		}
	}
	else if (type == MTD_NORFLASH)
	{
		my_printf("Found NOR flash\n");
		if (!flashcp(device, filename, 0, quiet, no_write))
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

int flash_ubi_jffs2_rootfs(char* device, char* filename, enum RootfsTypeEnum rootfs_type, int quiet, int no_write)
{
	int type = getFlashType(device);
	if (type == -1)
		return 0;

	if ((type == MTD_NANDFLASH || type == MTD_MLCNANDFLASH) && rootfs_type == UBIFS)
	{
		my_printf("Found NAND flash\n");
		if (!ubi_write(device, filename, quiet, no_write))
			return 0;
	}
	else if ((type == MTD_NANDFLASH || type == MTD_MLCNANDFLASH) && rootfs_type == JFFS2)
	{
		my_printf("Found NAND flash\n");
		if (!flash_erase_jffs2(device, "rootfs", quiet, no_write))
			return 0;
		if (!flash_write(device, filename, quiet, no_write))
			return 0;
	}
	else if (type == MTD_NORFLASH && rootfs_type == JFFS2)
	{
		my_printf("Found NOR flash\n");
		if (!flashcp(device, filename, 1, quiet, no_write))
			return 0;
	}
	else
	{
		my_fprintf(stderr, "Flash type \"%d\" in combination with rootfs type %d is not supported\n", type, rootfs_type);
		return 0;
	}

	return 1;
}

int setup_loop_device(const char* image, int quiet)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"losetup",		// program name
		"-r",			// read-only
		"-f",			// use next available loop device
		image,			// ubi image filename
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		my_printf("Setup loop device: losetup -f %s\n", image);
	if (losetup_main(argc, argv) != 0)
	{
		my_fprintf(stderr, "Error in setup losetup!\n");
		return 0;
	}
	my_printf("Using loop device: %s\n", ubi_loop_device);

	return 1;
}

int release_loop_device(int quiet)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"losetup",			// program name
		"-d",				// use next available loop device
		ubi_loop_device,	// loop device
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		my_printf("Release loop device: losetup -d %s\n", ubi_loop_device);
	if (losetup_main(argc, argv) != 0)
	{
		my_fprintf(stderr, "Error in releasing losetup!\n");
		return 0;
	}

	return 1;
}

int get_erasesize_and_writesize(unsigned int* erasesize, unsigned int* writesize, int quiet)
{
	// get erasesize and writesize from mtd0 device (has to be the same for the image file)
	char sysfs_path[256];
	FILE *fp;
	int ret;

	// Try Linux 4.x path first
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/mtd/mtd0/erasesize");
	fp = fopen(sysfs_path, "r");
	if (!fp)
	{
		// Try Linux 3.x path
		snprintf(sysfs_path, sizeof(sysfs_path), "/sys/devices/virtual/mtd/mtd0/erasesize");
		fp = fopen(sysfs_path, "r");
	}

	if (fp)
	{
		ret = fscanf(fp, "%u", erasesize);
		fclose(fp);
	}
	else
	{
		my_fprintf(stderr, "Error: Virtual erasesize sys file not present!\n");
		return 0;
	}

	// Try Linux 4.x path first for writesize
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/mtd/mtd0/writesize");
	fp = fopen(sysfs_path, "r");
	if (!fp)
	{
		// Try Linux 3.x path
		snprintf(sysfs_path, sizeof(sysfs_path), "/sys/devices/virtual/mtd/mtd0/writesize");
		fp = fopen(sysfs_path, "r");
	}

	if (fp)
	{
		ret = fscanf(fp, "%u", writesize);
		fclose(fp);
	}
	else
	{
		my_fprintf(stderr, "Error: Virtual writesize sys file not present!\n");
		return 0;
	}

	if (!quiet)
		my_printf("Found erasezise 0x%x and writesize 0x%x\n", *erasesize, *writesize);
	return 1;
}

int setup_block2mtd(const char* ubi_loop_device, unsigned int erasesize, unsigned int writesize)
{
	FILE *fp;

	my_printf("Setup block2mtd: ");

	fp = fopen("/sys/module/block2mtd/parameters/block2mtd", "w");
	if (!fp)
	{
		my_printf("Failed\n");
		return 0;
	}

	if (fprintf(fp, "%s,0x%x,0x%x", ubi_loop_device, erasesize, writesize) < 0)
	{
		my_printf("Failed\n");
		fclose(fp);
		return 0;
	}

	my_printf("Success\n");
	fclose(fp);
	return 1;
}

int remove_block2mtd(const char* ubi_loop_device)
{
	FILE *fp;

	fp = fopen("/sys/module/block2mtd/parameters/block2mtd", "w");
	if (!fp)
	{
		return 0;
	}

	if (fprintf(fp, "%s,remove", ubi_loop_device) < 0)
	{
		fclose(fp);
		my_fprintf(stderr, "Error: Remove block2mtd!\n");
		return 0;
	}

	fclose(fp);
	return 1;
}

int detect_vid_offset(const char* ubi_loop_device, unsigned int* vid_offset)
{
	FILE *fp;
	char buffer[4];
	int pos = 0;
	int ret;

	memset(buffer, 0, sizeof(buffer));

	my_printf("Detecting VID Offset: \n");

	fp = fopen(ubi_loop_device, "rb");
	if (!fp)
	{
		my_printf("Failed open device\n");
		return 0;
	}

	// find "UBI!"
	while (!feof(fp))
	{
		if (fgetc(fp) == 'U')
		{
			ret = fread(buffer, 3, 1, fp);
			if (ret == 1 && strcmp(buffer, "BI!") == 0)
			{
				*vid_offset = pos + 3;
				fclose(fp);
				my_printf("Found offset 0x%x\n", *vid_offset);
				return 1;
			}
		}
		pos++;
	}
	my_printf("Failed finding offset\n");
	fclose(fp);
	return 0;
}

int detect_mtd_device(const char* ubi_loop_device, unsigned int* mtd_device)
{
	int err;
	libmtd_t libmtd;
	struct mtd_info mtd_info;
	struct mtd_dev_info mtd;
	char mtd_name[100];

	my_printf("Detecting new mtd device: ");

	snprintf(mtd_name, sizeof(mtd_name), "block2mtd: %s", ubi_loop_device);

	libmtd = libmtd_open();
	if (libmtd == NULL)
	{
		my_printf("Failed open libmtd\n");
		return 0;
	}

	err = mtd_get_info(libmtd, &mtd_info);
	if (err)
	{
		my_printf("Failed reading mtd info\n");
		libmtd_close(libmtd);
		return 0;
	}

	for (int i = mtd_info.lowest_mtd_num; i <= mtd_info.highest_mtd_num; i++)
	{
		if (!mtd_dev_present(libmtd, i))
			continue;
		err = mtd_get_dev_info1(libmtd, i, &mtd);
		if (err)
			continue;

		if (strcmp(mtd.name, mtd_name) == 0)
		{
			my_printf("Found mtd%d\n", i);
			*mtd_device = i;
			return 1;
		}
	}

	my_printf("Failed finding device\n");
	libmtd_close(libmtd);
	return 0;
}

int extract_rootfs_from_nfi(const char* image, unsigned int writesize, int quiet)
{
	FILE *fp, *out;
	int ret;
	unsigned char buffer[10000];
	unsigned long totalsize, size, pos, partition;

	// rootfs image contains ecc data which needs to be removed
	unsigned int readbytes = (writesize / 32) + writesize;
	unsigned int writebytes = writesize;

	my_printf("Extracting rootfs from NFI\n");

	if (writesize > 5000)
	{
		my_printf("Not supported writesize %d\n", writesize);
		return 0;
	}

	fp = fopen(image, "rb");
	if (!fp)
	{
		my_printf("Failed to open nfi image\n");
		return 0;
	}

	// read nfi header
	ret = fread(buffer, 32, 1, fp);

	// check header
	if (ret != 1 || strncmp(buffer, "NFI", 3) != 0)
	{
		my_printf("Wrong NFI header\n");
		return 0;
	}

	// read totalsize
	ret = fread(buffer, 4, 1, fp);
	totalsize = (buffer[0] << 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3] + 36;
	if (ret != 1 && totalsize < 1000000)
	{
		my_printf("Wrong NFI header size\n");
		return 0;
	}
	my_printf("NFI header totalsize %lu\n", totalsize);

	partition = 0;
	pos = 32 + 4;

	// read partitions
	while (pos < totalsize)
	{
		// read size
		ret = fread(buffer, 4, 1, fp);
		if (ret != 1)
		{
			my_printf("NFI fread error");
			break;
		}
		size = (buffer[0] << 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];
		if (partition != 3)
		{	// skip
			ret = fseek(fp, size, SEEK_CUR);
			pos += size + 4;
			if (ret != 0)
			{
				my_printf("NFI fseek error");
				break;
			}
		}
		else
		{	// rootfs img
			pos += 4;
			unsigned int part_end = pos + size;
			my_printf("Found rootfs partition in NFI file with size %lu\n", size);
			strcat(nfi_path, "/ofg_nfi_rootfs.bin");
			FILE* fout = fopen(nfi_path, "wb");
			if (!fout)
			{
				my_printf("NFI failed to open output file\n");
				break;
			}
			while (pos < part_end)
			{
				ret = fread(buffer, readbytes, 1, fp);
				ret += fwrite(buffer, writebytes, 1, fout);
				if (ret != 2)
				{
					my_printf("NFI read/write failed\n");
					fclose(fout);
					fclose(fp);
					return 0;
				}
				pos += readbytes;
			}
			fclose(fout);
			fclose(fp);
			my_printf("Rootfs partition in NFI file successfully extracted now using %s\n", nfi_path);
			strcpy(image, nfi_path);
			return 1;
		}
		partition++;
	}
	fclose(fp);
	return 0;
}

int mount_ubi_image(char* image, char* nfi_filename, char* ubi_mount_path, int quiet, int no_write)
{
	unsigned int erasesize;
	unsigned int writesize;
	unsigned int vid_offset;
	int ret;

	my_printf("Mounting rootfs ubi image\n");
	if (no_write)
		return 1;

	if (!get_erasesize_and_writesize(&erasesize, &writesize, quiet))
	{
		return 0;
	}

	if (nfi_filename != '\0')
	{
		image = nfi_filename;
		if (!extract_rootfs_from_nfi(image, writesize, quiet))
			return 0;
	}

	if (!setup_loop_device(image, quiet))
		return 0;

	if (!setup_block2mtd(ubi_loop_device, erasesize, writesize))
	{
		release_loop_device(quiet);
		return 0;
	}

	if (!detect_vid_offset(ubi_loop_device, &vid_offset))
	{
		remove_block2mtd(ubi_loop_device);
		release_loop_device(quiet);
		return 0;
	}

	if (!detect_mtd_device(ubi_loop_device, &loop_mtd_device))
	{
		remove_block2mtd(ubi_loop_device);
		release_loop_device(quiet);
		return 0;
	}

	if (!ubi_attach_ofgwrite(loop_mtd_device, vid_offset, quiet, no_write))
	{
		remove_block2mtd(ubi_loop_device);
		release_loop_device(quiet);
		return 0;
	}

	if (mount("/dev/ubi10_0", ubi_mount_path, "ubifs", MS_RDONLY, NULL) < 0)
	{
		ubi_detach_dev(loop_mtd_device, quiet, no_write);
		remove_block2mtd(ubi_loop_device);
		release_loop_device(quiet);
		my_fprintf(stderr, "Error: Mounting ubi device\n");
		return 0;
	}

	return 1;
}

int umount_ubi_image(char* ubi_mount_path, int quiet, int no_write)
{
	int ret;

	my_printf("Unmounting rootfs ubi image\n");
	if (no_write)
		return 1;

	ret = umount(ubi_mount_path);
	ret = ubi_detach_dev(loop_mtd_device, quiet, no_write);
	ret = remove_block2mtd(ubi_loop_device);
	ret = release_loop_device(quiet);

	return 1;
}

int cp_busybox(char* source, char* target, int quiet, int no_write)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"cp",		// program name
		"-a",		// recursive, preserve symlinks and file attributes
		source,		// source directory
		target,		// target directory
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!no_write)
		if (cp_main(argc, argv) != 0)
		{
			my_fprintf(stderr, "Error: Busybox cp!\n");
			return 0;
		}

	return 1;
}

int cp_rootfs(char* source, char* target, int quiet, int no_write)
{
	DIR *dp;
	struct dirent *d;
	char new_source[1000];
	int overall_dir_entries = 0;
	int dir_entries = 0;

	if (!quiet)
		my_printf("Copy rootfs: cp -a -f %s %s\n", source, target);
	if (no_write)
		return 1;

	dp = opendir(source);
	if (dp == NULL)
	{
		my_printf("cp_rootfs: Failed to open source dir\n");
		return 0;
	}

	// count entries for progressbar
	while ((d = readdir(dp)) != NULL)
	{
		if (strcmp(d->d_name, ".") == 0
		 || strcmp(d->d_name, "..") == 0)
			continue;
		overall_dir_entries++;
	}

	rewinddir(dp);

	while ((d = readdir(dp)) != NULL)
	{
		strcpy(new_source, source);
		strcat(new_source, d->d_name);

		if (strcmp(d->d_name, ".") == 0
		 || strcmp(d->d_name, "..") == 0)
			continue;

		//my_printf("copying subdir %s to %s\n", new_source, target);
		if (!cp_busybox(new_source, target, quiet, no_write))
		{
			closedir(dp);
			return 0;
		}
		dir_entries++;
		set_step_progress((int)(dir_entries * 100 / overall_dir_entries));
	}
	closedir(dp);
	return 1;
}

int flash_ubi_loop_subdir(char* filename, char* nfi_filename, int quiet, int no_write)
{
	int ret;
	char rootfs_path[1000];
	char ubi_mount_path[1000];

	strcpy(rootfs_path, "/oldroot_remount/");
	strcat(rootfs_path, rootfs_sub_dir);
	strcat(rootfs_path, "/");

	strcpy(ubi_mount_path, "/ubi_mount/");
	if (!no_write)
	{
		mkdir(ubi_mount_path, 777);
	}

	if (!mount_ubi_image(filename, nfi_filename, ubi_mount_path, quiet, no_write))
	{
		return 0;
	}

	set_step("Deleting rootfs");
	if (!no_write)
	{
		ret = rm_rootfs(rootfs_path, quiet, no_write); // ignore return value as it always fails, because oldroot_remount cannot be removed
	}

	if (!no_write)
	{
		mkdir(rootfs_path, 777);
	}
	set_step("Copying rootfs");
	if (!cp_rootfs(ubi_mount_path, rootfs_path, quiet, no_write))
	{
		sync();
		umount_ubi_image(ubi_mount_path, quiet, no_write);
		if (!no_write)
		{
			rmdir(ubi_mount_path);
		}
		return 0;
	}
	sync();
	umount_ubi_image(ubi_mount_path, quiet, no_write);
	if (!no_write)
	{
		rmdir(ubi_mount_path);
	}

	return 1;
}
