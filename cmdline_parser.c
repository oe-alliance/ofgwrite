#include "ofgwrite.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>


// search device table for specific partition names
int search_via_part_names(char* device_table)
{
	int partition_number = 1;
	char device_name[100];
	char cmp_kernel_name[50];
	char cmp_rootfs_name[50];
	char* pos;

	// Search for rootfs and kernel partitions. Both have to be on the same device.
	if (strstr(device_table, "(kernel)") != NULL && strstr(device_table, "(dreambox-rootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(kernel)");
		strcpy(cmp_rootfs_name, "(dreambox-rootfs)");
	}
	else if (strstr(device_table, "(kernel)") != NULL && strstr(device_table, "(rootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(kernel)");
		strcpy(cmp_rootfs_name, "(rootfs)");
	}
	else if (strstr(device_table, "(ekernel)") != NULL && strstr(device_table, "(rootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(ekernel)");
		strcpy(cmp_rootfs_name, "(rootfs)");
	}
	else if (strstr(device_table, "(exkernel)") != NULL && strstr(device_table, "(exrootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(exkernel)");
		strcpy(cmp_rootfs_name, "(exrootfs)");
	}
	else if (strstr(device_table, "(boot)") != NULL && strstr(device_table, "(root)") != NULL)
	{
		strcpy(cmp_kernel_name, "(boot)");
		strcpy(cmp_rootfs_name, "(root)");
	}
	else if (strstr(device_table, "(linuxkernel)") != NULL && strstr(device_table, "(linuxrootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(linuxkernel)");
		strcpy(cmp_rootfs_name, "(linuxrootfs)");
	}
	else if ( access( "/dev/block/by-name/flag", F_OK) != -1 && current_rootfs_sub_dir[0] != '\0')
	{
		sprintf(cmp_kernel_name, "(linuxkernel%d)", multiboot_partition);
		strcpy(cmp_rootfs_name, "(rootfs)");
		sprintf(rootfs_sub_dir, "linuxrootfs%d", multiboot_partition);
	}
	else if (current_rootfs_sub_dir[0] != '\0') // box with rootSubDir feature
	{
		sprintf(cmp_kernel_name, "(linuxkernel%d)", multiboot_partition);
		strcpy(cmp_rootfs_name, "(userdata)");
		sprintf(rootfs_sub_dir, "linuxrootfs%d", multiboot_partition);
	}
	else
		return 0;

	// read device name
	if ((pos = strstr(device_table, ":")) == NULL)
	{
		my_printf("Error: No device name in /proc/cmdline blkdevparts: %s\n", device_table);
		return -1;
	}
	strncpy(device_name, device_table, pos - device_table);
	device_name[pos - device_table] = '\0';
	device_table = pos + 1;

	while (device_table)
	{
		if ((pos = strstr(device_table, ",")) != NULL)
			*pos = '\0';

		if (strstr(device_table, cmp_kernel_name) != NULL)
		{
			found_kernel_device = 1;
			kernel_flash_mode = TARBZ2;
			sprintf(kernel_device, "/dev/%sp%d", device_name, partition_number);
		}
		else if (strstr(device_table, cmp_rootfs_name) != NULL)
		{
			found_rootfs_device = 1;
			rootfs_flash_mode = TARBZ2;
			sprintf(rootfs_device, "/dev/%sp%d", device_name, partition_number);
		}

		if (pos)
			device_table = ++pos;
		else
			break;
		partition_number++;
	}

	if (found_kernel_device)
		my_printf("Found cmdLine kernel device: %s\n", kernel_device);
	if (found_rootfs_device)
		my_printf("Found cmdLine rootfs device: %s\n", rootfs_device);
	if (found_kernel_device && found_rootfs_device)
		return 1;
	else
	{
		my_printf("Error: Wrong formatted blkdevparts in /proc/cmdline\n");
		return -1;
	}
}

// check whether devices point to valid partitions
int search_current_used_partitions(char* device_table)
{
	int partition_number = 1;
	char device_name[100];
	char part_name[100];
	char* pos;

	// read device name
	if ((pos = strstr(device_table, ":")) == NULL && device_table != pos)
	{
		my_printf("Error: No device name in /proc/cmdline blkdevparts: %s\n", device_table);
		return -1;
	}
	pos[0] = '\0';
	sprintf(device_name, "/dev/%sp", device_table);
	device_table = pos + 1;

	if (strstr(current_rootfs_device, device_name) == NULL || strstr(current_kernel_device, device_name) == NULL)
		return -1; // rootfs or kernel are located on other device

	while (device_table)
	{
		if ((pos = strstr(device_table, ",")) != NULL)
			*pos = '\0';
		sprintf(part_name, "%s%d", device_name, partition_number);
		if (strstr(part_name, current_kernel_device) != NULL && strstr(device_table, "(linuxkernel") != NULL && current_kernel_device[0] != '\0')
		{
			found_kernel_device = 1;
			kernel_flash_mode = TARBZ2;
			strcpy(kernel_device, current_kernel_device);
		}
		else if (strstr(part_name, current_rootfs_device) != NULL && strstr(device_table, "(userdata)") != NULL && current_rootfs_device[0] != '\0')
		{
			found_rootfs_device = 1;
			rootfs_flash_mode = TARBZ2;
			strcpy(rootfs_device, current_rootfs_device);
		}

		if (pos)
			device_table = ++pos;
		else
			break;
		partition_number++;
	}

	if (found_kernel_device)
		my_printf("Using cmdLine kernel device: %s\n", kernel_device);
	if (found_rootfs_device)
		my_printf("Using cmdLine rootfs device: %s\n", rootfs_device);
	if (found_kernel_device && found_rootfs_device)
	{
		strcpy(rootfs_sub_dir, current_rootfs_sub_dir);
		return 1;
	}
	else
	{
		my_printf("Error: Wrong or missing kernel/root in /proc/cmdline\n");
		return -1;
	}
}

void parse_cmdline_partition_table(char* cmdline)
{
	int ret;
	char* next_device;
	char* end;

	// cut off rest of cmdline
	if ((end = strstr(cmdline, " ")) != NULL)
		*end = '\0';

	while (cmdline)
	{
		if ((next_device = strstr(cmdline, ";")) != NULL)
			*next_device = '\0';
		if (current_rootfs_sub_dir[0] != '\0' && multiboot_partition == -1)
			// flash current running image -> check whether devices point to valid partitions
			ret = search_current_used_partitions(cmdline);
		else
			ret = search_via_part_names(cmdline);
		if (ret != 0)
			break;

		if (next_device)
			cmdline = ++next_device;
		else
			break;
	}
}
