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
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	my_printf("Flashing rootfs: ubiformat %s -f %s\n", device, filename);
	if (!no_write)
		if (ubiformat_main(argc, argv) != 0)
			return 0;

	return 1;
}

int ubi_detach_dev(char* device, int quiet, int no_write)
{
	optind = 0; // reset getopt_long
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
		if (!ubi_detach_dev(device, quiet, no_write))
			return 0;
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