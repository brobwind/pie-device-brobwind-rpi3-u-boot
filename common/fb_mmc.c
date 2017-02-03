/*
 * Copyright 2014 Broadcom Corporation.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>
#include <blk.h>
#include <fastboot.h>
#include <fb_mmc.h>
#include <image-sparse.h>
#include <part.h>
#include <mmc.h>
#include <div64.h>

/*
 * FIXME: Ensure we always set these names via Kconfig once xxx_PARTITION is
 * migrated
 */
#ifndef CONFIG_FASTBOOT_GPT_NAME
#define CONFIG_FASTBOOT_GPT_NAME "gpt"
#endif


#ifndef CONFIG_FASTBOOT_MBR_NAME
#define CONFIG_FASTBOOT_MBR_NAME "mbr"
#endif

#define FASTBOOT_MAX_BLK_WRITE 16384
static ulong timer;

struct fb_mmc_sparse {
	struct blk_desc	*dev_desc;
};

static int part_get_info_by_name_or_alias(struct blk_desc *dev_desc,
		const char *name, disk_partition_t *info)
{
	int ret;

	ret = part_get_info_by_name(dev_desc, name, info);
	if (ret < 0) {
		/* strlen("fastboot_partition_alias_") + 32(part_name) + 1 */
		char env_alias_name[25 + 32 + 1];
		char *aliased_part_name;

		/* check for alias */
		strcpy(env_alias_name, "fastboot_partition_alias_");
		strncat(env_alias_name, name, 32);
		aliased_part_name = getenv(env_alias_name);
		if (aliased_part_name != NULL)
			ret = part_get_info_by_name(dev_desc,
					aliased_part_name, info);
	}
	return ret;
}

static lbaint_t fb_mmc_blk_write(struct blk_desc *block_dev, lbaint_t start,
		lbaint_t blkcnt, const void *buffer)
{
	lbaint_t blk = start;
	lbaint_t blks_written;
	lbaint_t cur_blkcnt;
	lbaint_t blks = 0;
	int i;
	for (i = 0; i < blkcnt; i += FASTBOOT_MAX_BLK_WRITE) {
		cur_blkcnt = min((int)blkcnt-i, FASTBOOT_MAX_BLK_WRITE);
		if (buffer != NULL) {
			timed_send_info(&timer, "writing");
			blks_written = blk_dwrite(block_dev, blk, cur_blkcnt,
					buffer+(i*block_dev->blksz));
		} else {
			timed_send_info(&timer, "erasing");
			blks_written = blk_derase(block_dev, blk, cur_blkcnt);
		}
		blk += blks_written;
		blks += blks_written;
	}
	return blks;
}

static lbaint_t fb_mmc_sparse_write(struct sparse_storage *info,
		lbaint_t blk, lbaint_t blkcnt, const void *buffer)
{
	struct fb_mmc_sparse *sparse = info->priv;
	struct blk_desc *dev_desc = sparse->dev_desc;

	return fb_mmc_blk_write(dev_desc, blk, blkcnt, buffer);
}

static lbaint_t fb_mmc_sparse_reserve(struct sparse_storage *info,
		lbaint_t blk, lbaint_t blkcnt)
{
	return blkcnt;
}

static void write_raw_image(struct blk_desc *dev_desc, disk_partition_t *info,
		const char *part_name, void *buffer,
		unsigned int download_bytes, char *response)
{
	lbaint_t blkcnt;
	lbaint_t blks;

	/* determine number of blocks to write */
	blkcnt = ((download_bytes + (info->blksz - 1)) & ~(info->blksz - 1));
	blkcnt = lldiv(blkcnt, info->blksz);

	if (blkcnt > info->size) {
		error("too large for partition: '%s'\n", part_name);
		fastboot_fail("too large for partition", response);
		return;
	}

	puts("Flashing Raw Image\n");

	blks = fb_mmc_blk_write(dev_desc, info->start, blkcnt, buffer);
	if (blks != blkcnt) {
		error("failed writing to device %d\n", dev_desc->devnum);
		fastboot_fail("failed writing to device", response);
		return;
	}

	printf("........ wrote " LBAFU " bytes to '%s'\n", blkcnt * info->blksz,
	       part_name);
	fastboot_okay("", response);
}

void fb_mmc_flash_write(const char *cmd, void *download_buffer,
			unsigned int download_bytes, char *response)
{
	struct blk_desc *dev_desc;
	disk_partition_t info;

	dev_desc = blk_get_dev("mmc", CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		error("invalid mmc device\n");
		fastboot_fail("invalid mmc device", response);
		return;
	}

#ifdef CONFIG_EFI_PARTITION
	if (strcmp(cmd, CONFIG_FASTBOOT_GPT_NAME) == 0) {
		printf("%s: updating MBR, Primary and Backup GPT(s)\n",
		       __func__);
		if (is_valid_gpt_buf(dev_desc, download_buffer)) {
			printf("%s: invalid GPT - refusing to write to flash\n",
			       __func__);
			fastboot_fail("invalid GPT partition", response);
			return;
		}
		if (write_mbr_and_gpt_partitions(dev_desc, download_buffer)) {
			printf("%s: writing GPT partitions failed\n", __func__);
			fastboot_fail("writing GPT partitions failed",
				      response);
			return;
		}
		printf("........ success\n");
		fastboot_okay("", response);
		return;
	}
#endif

#ifdef CONFIG_DOS_PARTITION
	if (strcmp(cmd, CONFIG_FASTBOOT_MBR_NAME) == 0) {
		printf("%s: updating MBR\n", __func__);
		if (is_valid_dos_buf(download_buffer)) {
			printf("%s: invalid MBR - refusing to write to flash\n",
			       __func__);
			fastboot_fail("invalid MBR partition", response);
			return;
		}
		if (write_mbr_partition(dev_desc, download_buffer)) {
			printf("%s: writing MBR partition failed\n", __func__);
			fastboot_fail("writing MBR partition failed", response);
			return;
		}
		printf("........ success\n");
		fastboot_okay("", response);
		return;
	}
#endif

	if (part_get_info_by_name_or_alias(dev_desc, cmd, &info) < 0) {
		error("cannot find partition: '%s'\n", cmd);
		fastboot_fail("cannot find partition", response);
		return;
	}

	if (is_sparse_image(download_buffer)) {
		struct fb_mmc_sparse sparse_priv;
		struct sparse_storage sparse;

		sparse_priv.dev_desc = dev_desc;

		sparse.blksz = info.blksz;
		sparse.start = info.start;
		sparse.size = info.size;
		sparse.write = fb_mmc_sparse_write;
		sparse.reserve = fb_mmc_sparse_reserve;

		printf("Flashing sparse image at offset " LBAFU "\n",
		       sparse.start);

		sparse.priv = &sparse_priv;
		write_sparse_image(&sparse, cmd, download_buffer,
				   download_bytes, response);
	} else {
		write_raw_image(dev_desc, &info, cmd, download_buffer,
				download_bytes, response);
	}
}

void fb_mmc_erase(const char *cmd, char *response)
{
	int ret;
	struct blk_desc *dev_desc;
	disk_partition_t info;
	lbaint_t blks, blks_start, blks_size, grp_size;
	struct mmc *mmc = find_mmc_device(CONFIG_FASTBOOT_FLASH_MMC_DEV);

	if (mmc == NULL) {
		error("invalid mmc device");
		fastboot_fail("invalid mmc device", response);
		return;
	}

	dev_desc = blk_get_dev("mmc", CONFIG_FASTBOOT_FLASH_MMC_DEV);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		error("invalid mmc device");
		fastboot_fail("invalid mmc device", response);
		return;
	}

	ret = part_get_info_by_name_or_alias(dev_desc, cmd, &info);
	if (ret < 0) {
		error("cannot find partition: '%s'", cmd);
		fastboot_fail("cannot find partition", response);
		return;
	}

	/* Align blocks to erase group size to avoid erasing other partitions */
	grp_size = mmc->erase_grp_size;
	blks_start = (info.start + grp_size - 1) & ~(grp_size - 1);
	if (info.size >= grp_size)
		blks_size = (info.size - (blks_start - info.start)) &
				(~(grp_size - 1));
	else
		blks_size = 0;

	printf("Erasing blocks " LBAFU " to " LBAFU " due to alignment\n",
	       blks_start, blks_start + blks_size);

	blks = fb_mmc_blk_write(dev_desc, blks_start, blks_size, NULL);
	if (blks != blks_size) {
		error("failed erasing from device %d", dev_desc->devnum);
		fastboot_fail("failed erasing from device", response);
		return;
	}

	printf("........ erased " LBAFU " bytes from '%s'\n",
	       blks_size * info.blksz, cmd);
	fastboot_okay("", response);
}
