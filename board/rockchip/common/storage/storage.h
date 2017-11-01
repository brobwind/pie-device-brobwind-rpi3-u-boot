/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 * Peter, Software Engineering, <superpeter.cai@gmail.com>.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#ifndef _STORAGE_H
#define _STORAGE_H

/* Interface declaration */
int vendor_storage_init(char* part_name);
int vendor_storage_read(u16 id, void *pbuf, u16 size);
int vendor_storage_write(u16 id, void *pbuf, u16 size);

#endif /* _STORAGE_H */

