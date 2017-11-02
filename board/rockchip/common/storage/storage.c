/*
 * (C) Copyright 2008 Fuzhou Rockchip Electronics Co., Ltd
 * Peter, Software Engineering, <superpeter.cai@gmail.com>.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <mmc.h>
#include <part.h>
#include <malloc.h>

#include "storage.h"

#define VENDOR_TAG			0x524B5644 /* tag for vendor check */
#define VENDOR_PART_NUM		4 /* The VendorStorage partition contains the number of Vendor blocks */
#define VENDOR_PART_SIZE	128 /* The number of memory blocks used by each Vendor structure(128*512Bytes=64KB) */
#define VENDOR_ITEM_NUM		126 /* The maximum number of items in each Vendor block */
#define VENDOR_BTYE_ALIGN	0x3F /* align to 64 bytes */

struct vendor_item {
	u16  id;
	u16  offset;
	u16  size;
	u16  flag;
};

struct vendor_info {
	u32	tag;
	/* Together with version2, it is used to select
	 * which Vendor block is currently active.
	 */
	u32	version;
	u16	next_index;
	u16	item_num;
	u16	free_offset;
	u16	free_size;
	struct vendor_item item[VENDOR_ITEM_NUM];
	/* The entire Vendor block size is:VENDOR_PART_SIZE * 512Bytes=64KB.
	 * The total size of the fields above this comment is:4B+4B+2B+2B+2B+2+VENDOR_ITEM_NUM*8B=1024B.
	 * The total size of the fields after the data element is:4B.
	 * So the size of the data element is:(VENDOR_PART_SIZE * 512B)-1024B-4B.
	 */
	u8	data[VENDOR_PART_SIZE * 512 - 1024 - 4];
	/* Used to ensure the current Vendor block content integrity.
	 * Normally, the value is equal to version;
	 * Others, the contents of this Vendor block is incorrect.
	 */
	u32	version2;
};

static struct vendor_info g_vendor;

static int vendor_ops(u8 *buffer, u32 addr, u32 n_sec, int write)
{
	struct blk_desc *dev_desc;
	disk_partition_t part_info;
	char* part_name = NULL;
	unsigned long ret = 0; /* return value */

	/* get vendor_storage name */
	part_name = getenv("RkVendorStorage#");
	if(!part_name){
		printf("ERROR:%s RkVendorStorage# is not set.\n", __func__);
		return -EPERM;
	}
    dev_desc = blk_get_dev("mmc", 0);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		printf("ERROR:%s Invalid mmc device\n", __func__);
		return -ENODEV;
	}
    if (part_get_info_by_name(dev_desc, part_name, &part_info) < 0) {
		printf("ERROR:%s Could not find %s partition\n", __func__, part_name);
		return -EINVAL;
	}

	if (write)
		ret = blk_dwrite(dev_desc, addr+part_info.start, n_sec, buffer);
	else
		ret = blk_dread(dev_desc, addr+part_info.start, n_sec, buffer);

	return ((ret>0) ? 0 : (-1));
}

int vendor_storage_init(char* part_name)
{
	u32 i, max_ver, max_index;
	u8 *p_buf;
	int ret;

	/* set vendor storage part name */
	setenv("RkVendorStorage#", part_name);
	max_ver = 0;
	max_index = 0;
	for (i = 0; i < VENDOR_PART_NUM; i++) {
		/* read first 512 bytes */
		p_buf = (u8 *)&g_vendor;
		ret = vendor_ops(p_buf, VENDOR_PART_SIZE * i, 1, 0);
		if (ret)
			return ret;
		/* read last 512 bytes */
		p_buf += (VENDOR_PART_SIZE - 1) * 512;
		ret = vendor_ops(p_buf, VENDOR_PART_SIZE * (i + 1) - 1, 1, 0);
		if (ret)
			return ret;
		if ((g_vendor.tag == VENDOR_TAG) &&
		    (g_vendor.version2 == g_vendor.version)) {
			if (max_ver < g_vendor.version) {
				max_index = i;
				max_ver = g_vendor.version;
			}
		}
	}

	if (max_ver) {
		ret = vendor_ops((u8 *) &g_vendor, VENDOR_PART_SIZE * max_index, VENDOR_PART_SIZE, 0);
		if (ret)
			return ret;
	} else {
		memset(&g_vendor, 0, sizeof(g_vendor));
		g_vendor.version = 1;
		g_vendor.tag = VENDOR_TAG;
		g_vendor.version2 = g_vendor.version;
		g_vendor.free_size = sizeof(g_vendor.data);
	}

	return 0;
}

int vendor_storage_read(u16 id, void *pbuf, u16 size)
{
	u32 i;
	for (i = 0; i < g_vendor.item_num; i++) {
		if (g_vendor.item[i].id == id) {
			if (size > g_vendor.item[i].size)
				size = g_vendor.item[i].size;
			memcpy(pbuf, &g_vendor.data[g_vendor.item[i].offset], size);
			return size;
		}
	}

	return -EINVAL;
}

int vendor_storage_write(u16 id, void *pbuf, u16 size)
{
	u32 i, align_size;
	u16 next_index;
	struct vendor_item *item;

	next_index = g_vendor.next_index;
	align_size = (size + VENDOR_BTYE_ALIGN) & (~VENDOR_BTYE_ALIGN);
	if (size > align_size)
		return -EINVAL;
	for (i = 0; i < g_vendor.item_num; i++) {
		if (g_vendor.item[i].id == id) {
			memcpy(&g_vendor.data[g_vendor.item[i].offset], pbuf, size);
			g_vendor.item[i].size = size;
			g_vendor.version++;
			g_vendor.version2 = g_vendor.version;
			g_vendor.next_index++;
			if (g_vendor.next_index >= VENDOR_PART_NUM)
				g_vendor.next_index = 0;
			return vendor_ops((u8 *) &g_vendor, VENDOR_PART_SIZE * next_index,
						VENDOR_PART_SIZE, 1);
		}
	}
	if (g_vendor.free_size >= align_size) {
		item = &g_vendor.item[g_vendor.item_num];
		item->id = id;
		item->offset = g_vendor.free_offset;
		item->size = size;
		g_vendor.free_offset += align_size;
		g_vendor.free_size -= align_size;
		memcpy(&g_vendor.data[item->offset], pbuf, size);
		g_vendor.item_num++;
		g_vendor.version++;
		g_vendor.version2 = g_vendor.version;
		g_vendor.next_index++;
		if (g_vendor.next_index >= VENDOR_PART_NUM)
			g_vendor.next_index = 0;
		return vendor_ops((u8 *) &g_vendor, VENDOR_PART_SIZE * next_index,
					VENDOR_PART_SIZE, 1);
	}

	return -ENOSPC;
}
