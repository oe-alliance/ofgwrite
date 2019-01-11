#include "ofgwrite.h"

#include <stdio.h>
#include <string.h>


int parse_device_table(char* device_table)
{
	int partition_number = 1;
	char device_name[100];
	char cmp_kernel_name[50];
	char cmp_rootfs_name[50];
	char* pos;

	// Search for rootfs and kernel partitions. Both have to be on the same device.
	if (strstr(device_table, "(kernel)") != NULL && strstr(device_table, "(rootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(kernel)");
		strcpy(cmp_rootfs_name, "(rootfs)");
	}
	else if (strstr(device_table, "(linuxkernel)") != NULL && strstr(device_table, "(linuxrootfs)") != NULL)
	{
		strcpy(cmp_kernel_name, "(linuxkernel)");
		strcpy(cmp_rootfs_name, "(linuxrootfs)");
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
			sprintf(kernel_device, "/dev/%sp%d", device_name, partition_number);
		}
		else if (strstr(device_table, cmp_rootfs_name) != NULL)
		{
			found_rootfs_device = 1;
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
		ret = parse_device_table(cmdline);
		if (ret != 0)
			break;

		if (next_device)
			cmdline = ++next_device;
		else
			break;
	}
}
