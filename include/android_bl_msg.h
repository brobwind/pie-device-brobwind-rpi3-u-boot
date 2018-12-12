/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * This file was taken from the AOSP Project.
 * Repository: https://android.googlesource.com/platform/bootable/recovery/
 * File: bootloader_message/include/bootloader_message/bootloader_message.h
 * Commit: 8b309f6970ab3b7c53cc529c51a2cb44e1c7a7e1
 *
 * Copyright (C) 2008 The Android Open Source Project
 */

#ifndef __ANDROID_BL_MSG_H
#define __ANDROID_BL_MSG_H

/*
 * compiler.h defines the types that otherwise are included from stdint.h and
 * stddef.h
 */
#include <compiler.h>
#include <linux/sizes.h>

/*
 * Spaces used by misc partition are as below:
 * 0   - 2K     Bootloader Message
 * 2K  - 16K    Used by Vendor's bootloader (the 2K - 4K range may be optionally
 *              used as bootloader_message_ab struct)
 * 16K - 64K    Used by uncrypt and recovery to store wipe_package for A/B
 *              devices
 * Note that these offsets are admitted by bootloader, recovery and uncrypt, so
 * they are not configurable without changing all of them.
 */
#define ANDROID_MISC_BM_OFFSET		0
#define ANDROID_MISC_WIPE_OFFSET	SZ_16K

/**
 * Bootloader Message (2-KiB).
 *
 * This structure describes the content of a block in flash
 * that is used for recovery and the bootloader to talk to
 * each other.
 *
 * The command field is updated by linux when it wants to
 * reboot into recovery or to update radio or bootloader firmware.
 * It is also updated by the bootloader when firmware update
 * is complete (to boot into recovery for any final cleanup)
 *
 * The status field is written by the bootloader after the
 * completion of an "update-radio" or "update-hboot" command.
 *
 * The recovery field is only written by linux and used
 * for the system to send a message to recovery or the
 * other way around.
 *
 * The stage field is written by packages which restart themselves
 * multiple times, so that the UI can reflect which invocation of the
 * package it is.  If the value is of the format "#/#" (eg, "1/3"),
 * the UI will add a simple indicator of that status.
 *
 * We used to have slot_suffix field for A/B boot control metadata in
 * this struct, which gets unintentionally cleared by recovery or
 * uncrypt. Move it into struct bootloader_message_ab to avoid the
 * issue.
 */
struct andr_bl_msg {
	char command[32];
	char status[32];
	char recovery[768];

	/*
	 * The 'recovery' field used to be 1024 bytes.  It has only ever
	 * been used to store the recovery command line, so 768 bytes
	 * should be plenty.  We carve off the last 256 bytes to store the
	 * stage string (for multistage packages) and possible future
	 * expansion.
	 */
	char stage[32];

	/*
	 * The 'reserved' field used to be 224 bytes when it was initially
	 * carved off from the 1024-byte recovery field. Bump it up to
	 * 1184-byte so that the entire bootloader_message struct rounds up
	 * to 2048-byte.
	 */
	char reserved[1184];
};

/**
 * The A/B-specific bootloader message structure (4-KiB).
 *
 * We separate A/B boot control metadata from the regular bootloader
 * message struct and keep it here. Everything that's A/B-specific
 * stays after struct bootloader_message, which should be managed by
 * the A/B-bootloader or boot control HAL.
 *
 * The slot_suffix field is used for A/B implementations where the
 * bootloader does not set the androidboot.ro.boot.slot_suffix kernel
 * commandline parameter. This is used by fs_mgr to mount /system and
 * other partitions with the slotselect flag set in fstab. A/B
 * implementations are free to use all 32 bytes and may store private
 * data past the first NUL-byte in this field. It is encouraged, but
 * not mandatory, to use 'struct bootloader_control' described below.
 *
 * The update_channel field is used to store the Omaha update channel
 * if update_engine is compiled with Omaha support.
 */
struct andr_bl_msg_ab {
	struct andr_bl_msg message;
	char slot_suffix[32];
	char update_channel[128];

	/* Round up the entire struct to 4096-byte */
	char reserved[1888];
};

#define ANDROID_BOOT_CTRL_MAGIC   0x42414342 /* Bootloader Control AB */
#define ANDROID_BOOT_CTRL_VERSION 1

struct andr_slot_metadata {
	/*
	 * Slot priority with 15 meaning highest priority, 1 lowest
	 * priority and 0 the slot is unbootable
	 */
	u8 priority : 4;
	/* Number of times left attempting to boot this slot */
	u8 tries_remaining : 3;
	/* 1 if this slot has booted successfully, 0 otherwise */
	u8 successful_boot : 1;
	/*
	 * 1 if this slot is corrupted from a dm-verity corruption,
	 * 0 otherwise
	 */
	u8 verity_corrupted : 1;
	/* Reserved for further use */
	u8 reserved : 7;
} __packed;

/**
 * Bootloader Control AB.
 *
 * This struct can be used to manage A/B metadata. It is designed to
 * be put in the 'slot_suffix' field of the 'bootloader_message'
 * structure described above. It is encouraged to use the
 * 'bootloader_control' structure to store the A/B metadata, but not
 * mandatory.
 */
struct andr_bl_control {
	/* NULL terminated active slot suffix */
	char slot_suffix[4];
	/* Bootloader Control AB magic number (see BOOT_CTRL_MAGIC) */
	u32 magic;
	/* Version of struct being used (see BOOT_CTRL_VERSION) */
	u8 version;
	/* Number of slots being managed */
	u8 nb_slot : 3;
	/* Number of times left attempting to boot recovery */
	u8 recovery_tries_remaining : 3;
	/* Ensure 4-bytes alignment for slot_info field */
	u8 reserved0[2];
	/* Per-slot information. Up to 4 slots */
	struct andr_slot_metadata slot_info[4];
	/* Reserved for further use */
	u8 reserved1[8];
	/*
	 * CRC32 of all 28 bytes preceding this field (little endian
	 * format)
	 */
	u32 crc32_le;
} __packed;

#endif  /* __ANDROID_BL_MSG_H */
