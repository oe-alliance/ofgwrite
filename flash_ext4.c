#include <stdio.h>
#include <getopt.h>

int flash_ext4_kernel(char* device, char* filename, off_t kernel_file_size, int quiet, int no_write)
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

	set_step("Writing ext4 kernel");
	int ret;
	long long readBytes = 0;
	int current_percent = 0;
	int new_percent     = 0;
	while (!feof(kernel_file))
	{
		// Don't add my_printf for debugging! Debug messages will be written to kernel device!
		ret = fread(buffer, 1, sizeof(buffer), kernel_file);
		if (ret == 0)
		{
			my_printf("Error reading kernel file.\n");
			fclose(kernel_file);
			fclose(kernel_dev);
			return 0;
		}
		readBytes += ret;
		new_percent = readBytes * 100/ kernel_file_size;
		if (current_percent < new_percent)
		{
			set_step_progress(new_percent);
			current_percent = new_percent;
		}
		if (!no_write)
		{
			ret = fwrite(buffer, ret, 1, kernel_dev);
			if (ret != 1)
			{
				my_printf("Error writing kernel file to kernel device.\n");
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

int rm_rootfs(char* directory, int quiet, int no_write)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"rm",		// program name
		"-r",		// recursive
		"-f",		// force
		directory,	// directory
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		my_printf("Delete rootfs: rm -r -f %s\n", directory);
	if (!no_write)
		if (rm_main(argc, argv) != 0)
			return 0;

	return 1;
}

int untar_rootfs(char* filename, char* directory, int quiet, int no_write)
{
	optind = 0; // reset getopt_long
	char* argv[] = {
		"tar",		// program name
		"-x",		// extract
		"-f",
		filename,	// file
		"-C",
		directory,	// untar to directory
		NULL
	};
	int argc = (int)(sizeof(argv) / sizeof(argv[0])) - 1;

	if (!quiet)
		my_printf("Untar: tar xf %s\n", filename);
	if (!no_write)
		if (tar_main(argc, argv) != 0)
			return 0;

	return 1;
}

int flash_ext4_rootfs(char* filename, int quiet, int no_write)
{
	int ret;
	// instead of creating new filesystem just delete whole content
	set_step("deleting ext4 rootfs");
	if (!no_write)
	{
		ret = rm_rootfs("/oldroot", quiet, no_write); // ignore return value as it always fails, because oldroot cannot be removed
	}

	set_step("Writing ext4 rootfs");
	set_step_progress(0);
	if (!untar_rootfs(filename, "/oldroot/", quiet, no_write))
	{
		my_printf("Error writing ext4 rootfs\n");
		return 0;
	}
	sync();
	return 1;
}