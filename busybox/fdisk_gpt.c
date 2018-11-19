#include "../ofgwrite.h"

#if ENABLE_FEATURE_GPT_LABEL
/*
 * Copyright (C) 2010 Kevin Cernekee <cernekee@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#define GPT_MAGIC 0x5452415020494645ULL
enum {
	LEGACY_GPT_TYPE = 0xee,
	GPT_MAX_PARTS   = 256,
	GPT_MAX_PART_ENTRY_LEN = 4096,
	GUID_LEN        = 16,
};

typedef struct {
	uint64_t magic;
	uint32_t revision;
	uint32_t hdr_size;
	uint32_t hdr_crc32;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable_lba;
	uint64_t last_usable_lba;
	uint8_t  disk_guid[GUID_LEN];
	uint64_t first_part_lba;
	uint32_t n_parts;
	uint32_t part_entry_len;
	uint32_t part_array_crc32;
} gpt_header;

typedef struct {
	uint8_t  type_guid[GUID_LEN];
	uint8_t  part_guid[GUID_LEN];
	uint64_t lba_start;
	uint64_t lba_end;
	uint64_t flags;
	uint16_t name[36];
} gpt_partition;

static gpt_header *gpt_hdr;

static char *part_array;
static unsigned int n_parts;
static unsigned int part_array_len;
static unsigned int part_entry_len;

static inline gpt_partition *
gpt_part(int i)
{
	if (i >= n_parts) {
		return NULL;
	}
	return (gpt_partition *)&part_array[i * part_entry_len];
}

static uint32_t
gpt_crc32(void *buf, int len)
{
	return ~crc32_block_endian0(0xffffffff, buf, len, global_crc32_table);
}

static void
gpt_print_guid(uint8_t *buf)
{
	printf(
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		buf[3], buf[2], buf[1], buf[0],
		buf[5], buf[4],
		buf[7], buf[6],
		buf[8], buf[9],
		buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
}

/* TODO: real unicode support */
static void
gpt_print_wide(uint16_t *s, int max_len)
{
	int i = 0;

	while (i < max_len) {
		if (*s == 0)
			return;
		fputc(*s, stdout);
		s++;
	}
}

static void
gpt_list_table(int xtra UNUSED_PARAM)
{
	int i;
	char numstr6[6];
	// adapted for ofgwrite
	/*smart_ulltoa5(total_number_of_sectors * sector_size, numstr6, " KMGTPEZY")[0] = '\0';
	printf("Disk %s: %llu sectors, %s\n", disk_device,
		(unsigned long long)total_number_of_sectors,
		numstr6);
	printf("Logical sector size: %u\n", sector_size);
	printf("Disk identifier (GUID): ");
	gpt_print_guid(gpt_hdr->disk_guid);
	printf("\nPartition table holds up to %u entries\n",
		(int)SWAP_LE32(gpt_hdr->n_parts));
	printf("First usable sector is %llu, last usable sector is %llu\n\n",
		(unsigned long long)SWAP_LE64(gpt_hdr->first_usable_lba),
		(unsigned long long)SWAP_LE64(gpt_hdr->last_usable_lba));
*/
	//puts("Number  Start (sector)    End (sector)  Size       Code  Name");
	char partname[19];
	char kernel_name[7];
	char rootfs_name[7];
	int found_kernel = 0;
	int found_rootfs = 0;
	if (multiboot_partition != -1)
	{
		sprintf(kernel_name, "kernel%d", multiboot_partition);
		sprintf(rootfs_name, "rootfs%d", multiboot_partition);
	}
	else
	{
		strcpy(kernel_name, "kernel");
		strcpy(rootfs_name, "rootfs");
	}
	for (i = 0; i < n_parts; i++) {
		gpt_partition *p = gpt_part(i);
		if (p->lba_start) {
			/*smart_ulltoa5((1 + SWAP_LE64(p->lba_end) - SWAP_LE64(p->lba_start)) * sector_size,
				numstr6, " KMGTPEZY")[0] = '\0';
			printf("%4u %15llu %15llu %11s   %04x  ",
				i + 1,
				(unsigned long long)SWAP_LE64(p->lba_start),
				(unsigned long long)SWAP_LE64(p->lba_end),
				numstr6,
				0x0700 /* FIXME *//*);
			gpt_print_wide(p->name, 18);
			bb_putchar('\n');*/
			// adapted for ofgwrite: ignore upper byte as we only need us ascii chars
			int k;
			for (k = 0; k<19; k++)
				partname[k] = (char)p->name[k];
			if (strcmp(partname, kernel_name) == 0)
			{
				ext4_kernel_dev_found(disk_device, i+1);
				found_kernel = 1;
			}
			if (strcmp(partname, rootfs_name) == 0)
			{
				ext4_rootfs_dev_found(disk_device, i+1);
				found_rootfs = 1;
			}
			if ((user_kernel || user_rootfs) && (strcmp(partname, "bp30") == 0 || strcmp(partname, "bp31") == 0))
			{
				char dummy_device[1000];
				sprintf(dummy_device, "%sp%d", disk_device+5, i+1);  // disk_device+5 because disk_device includes /dev/
				if ( (user_kernel && (strcmp(kernel_device_arg, dummy_device) == 0))
				  || (user_rootfs && (strcmp(rootfs_device_arg, dummy_device) == 0)) )
				{
					my_printf("User specified device is a bp30/bp31 partition. These partitions shouldn't be used. Never!\nAborting...\n");
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	// If kernel OR rootfs found, return. If one is missing, handle error later. Don't search for other partitions.
	// If multiboot partition was specified, return also as user wanted to use a specific partition which was not found.
	if (found_kernel || found_rootfs || multiboot_partition != -1)
		return;

	my_printf("No matching partition names are found. Use current kernel and rootfs devices\n");

	// E.g. hd51 in single boot configuration with kernel1 and rootfs1 partitions
	// or user hasn't specified multiboot partition on a multiboot box like hd51.
	// In both cases use current used kernel and rootfs devices

	// find partition name of current rootfs device
	int part_num = -1;
	if (sscanf(current_rootfs_device, "%*[a-z/]%*dp%d", &part_num) == EOF)
		return;

	// No partition number found. Device name is not as expected
	if (part_num == -1)
	{
		my_printf("Error: Partition number not found. Device name: %s\n", current_rootfs_device);
		return;
	}

	if (part_num <= n_parts)
	{
		gpt_partition *p = gpt_part(part_num - 1);
		if (p->lba_start) {
			int k;
			for (k = 0; k<19; k++)
				partname[k] = (char)p->name[k];
			// expecting names starting with "rootfs" and after that a number. So e.g. rootfs3
			if (sscanf(partname, "%*[a-z]%d", &multiboot_partition) == EOF)
				return;
			my_printf("Using current multiboot partition %d\n", multiboot_partition);
		}
	}

	if (multiboot_partition != -1)
	{
		sprintf(kernel_name, "kernel%d", multiboot_partition);
		sprintf(rootfs_name, "rootfs%d", multiboot_partition);
	}
	else
		return;

	// now search for both partitions as we need to call both ext4_..._dev_found functions
	for (i = 0; i < n_parts; i++) {
		gpt_partition *p = gpt_part(i);
		if (p->lba_start) {
			int k;
			for (k = 0; k<19; k++)
				partname[k] = (char)p->name[k];
			if (strcmp(partname, kernel_name) == 0)
			{
				ext4_kernel_dev_found(disk_device, i+1);
			}
			if (strcmp(partname, rootfs_name) == 0)
			{
				ext4_rootfs_dev_found(disk_device, i+1);
			}
		}
	}
}

static int
check_gpt_label(void)
{
	struct partition *first = pt_offset(MBRbuffer, 0);
	struct pte pe;
	uint32_t crc;

	/* LBA 0 contains the legacy MBR */

	if (!valid_part_table_flag(MBRbuffer)
	 || first->sys_ind != LEGACY_GPT_TYPE
	) {
		current_label_type = 0;
		return 0;
	}

	/* LBA 1 contains the GPT header */

	read_pte(&pe, 1);
	gpt_hdr = (void *)pe.sectorbuffer;

	if (gpt_hdr->magic != SWAP_LE64(GPT_MAGIC)) {
		current_label_type = 0;
		return 0;
	}

	if (!global_crc32_table) {
		global_crc32_table = crc32_filltable(NULL, 0);
	}

	crc = SWAP_LE32(gpt_hdr->hdr_crc32);
	gpt_hdr->hdr_crc32 = 0;
	if (gpt_crc32(gpt_hdr, SWAP_LE32(gpt_hdr->hdr_size)) != crc) {
		/* FIXME: read the backup table */
		puts("\nwarning: GPT header CRC is invalid\n");
	}

	n_parts = SWAP_LE32(gpt_hdr->n_parts);
	part_entry_len = SWAP_LE32(gpt_hdr->part_entry_len);
	if (n_parts > GPT_MAX_PARTS
	 || part_entry_len > GPT_MAX_PART_ENTRY_LEN
	 || SWAP_LE32(gpt_hdr->hdr_size) > sector_size
	) {
		puts("\nwarning: unable to parse GPT disklabel\n");
		current_label_type = 0;
		return 0;
	}

	part_array_len = n_parts * part_entry_len;
	part_array = xmalloc(part_array_len);
	seek_sector(SWAP_LE64(gpt_hdr->first_part_lba));
	if (full_read(dev_fd, part_array, part_array_len) != part_array_len) {
		fdisk_fatal(unable_to_read);
	}

	if (gpt_crc32(part_array, part_array_len) != gpt_hdr->part_array_crc32) {
		/* FIXME: read the backup table */
		puts("\nwarning: GPT array CRC is invalid\n");
	}

	puts("Found valid GPT with protective MBR; using GPT\n");

	current_label_type = LABEL_GPT;
	return 1;
}

#endif /* GPT_LABEL */
