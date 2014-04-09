/*
 * FileName: kernel/include/linux/mtd/nve_interface.h
 * Description: define struct nve_info_user and declear function
 * nve_direct_access that will be used by other functions or files.
 * if you want to visit NV partition, i.e. read NV items or write NV
 * items in other files, you should include this .h file.
 * Copyright (C) Hisilicon technologies Co., Ltd All rights reserved.
 * Revision history:
 */
#ifndef __NVE_INTERFACE_H__
#define __NVE_INTERFACE_H__

#include <linux/types.h>

#define NV_NAME_LENGTH          8       /*NV name maximum length*/
#define NVE_NV_DATA_SIZE        104     /*NV data maximum length*/

#define NV_READ                 1       /*NV read  operation*/
#define NV_WRITE                0       /*NV write operation*/



struct nve_info_user {
	uint32_t nv_operation;              /*0-write,1-read*/
	uint32_t nv_number;                 /*NV number you want to visit*/
	char nv_name[NV_NAME_LENGTH];
	uint32_t valid_size;
	u_char nv_data[NVE_NV_DATA_SIZE];
};

extern int nve_direct_access(struct nve_info_user * user_info);

#endif

