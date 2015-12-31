#include <stdio.h>

int flash_ext4_kernel(char* device, char* filename, int quiet, int no_write)
{
	char buffer[512];

	// Open kernel file
	FILE* kernel_file;
	kernel_file = fopen(filename, "rb");
	if (kernel_file == NULL)
	{
		my_printf("Error while opening kernel file %s\n", filename);
		return 0;
	}

	// Open kernel device
	FILE* kernel_dev;
	kernel_dev = fopen(device, "wb");
	if (kernel_dev == NULL)
	{
		my_printf("Error while opening kernel device %s\n", device);
		return 0;
	}

	int ret;
	while (!feof(kernel_file))
	{
		ret = fread(buffer, 1, sizeof(buffer), kernel_file);
		if (ret == 0)
		{
			my_printf("Error reading kernel file.");
			fclose(kernel_file);
			fclose(kernel_dev);
			return 0;
		}
		if (!no_write)
		{
			ret = fwrite(buffer, ret, 1, kernel_dev);
			if (ret != 1)
			{
				my_printf("Error writing kernel file to kernel device.");
				fclose(kernel_file);
				fclose(kernel_dev);
				return 0;
			}
		}
	}

	fclose(kernel_file);
	fclose(kernel_dev);

	return 1;
}

int flash_ext4_rootfs(char* device, char* filename)
{
	return 1;
}