/*
 * Copyright (c) 2011 Hisilicon Technologies Co., Ltd. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined(IPPS_H)
#define IPPS_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/cpufreq.h>

#define IPPS_DEVICE_NAME_MAX 64

#define	IPPS_POLICY_NORMAL		(0)
#define IPPS_POLICY_POWERSAVE	(1)
#define IPPS_POLICY_PERFORMANCE	(2)

#define IPPS_OBJ_CPU	(0x01)
#define IPPS_OBJ_GPU	(0x02)
#define IPPS_OBJ_DDR	(0x04)
#define IPPS_OBJ_TEMP	(0x08)

#define IPPS_ENABLE		(0x01)
#define IPPS_DISABLE	(0x00)


enum ipps_cmd_type {
	IPPS_GET_FREQS_TABLE,
	IPPS_GET_CUR_FREQ,
	IPPS_SET_CUR_FREQ,
	IPPS_GET_CUR_POLICY,
	IPPS_SET_CUR_POLICY,
	IPPS_GET_PARAM,
	IPPS_SET_PARAM,
	IPPS_GET_MODE,
	IPPS_SET_MODE,
};

struct ipps_freq {
	unsigned int min_freq;
	unsigned int max_freq;
	unsigned int safe_freq;
	unsigned int block_freq;
};

struct ipps_temp {
	int safe;
	int alarm;
	int reset;
};

struct ipps_param {
	struct ipps_freq cpu;
	struct ipps_freq gpu;
	struct ipps_freq ddr;
	struct ipps_temp temp;
};

struct ipps_device {
	char	name[IPPS_DEVICE_NAME_MAX];

	spinlock_t			client_data_lock;
	struct list_head	core_list;
	struct list_head	client_data_list;

	struct device		*dev;

	unsigned int		object;

	int (*command) (struct device *dev,
			enum ipps_cmd_type cmd,
			unsigned int object,
			void *data);

	enum {
		IPPS_DEV_UNINITIALIZED,
		IPPS_DEV_REGISTERED,
		IPPS_DEV_UNREGISTERED
	}reg_state;

};

struct ipps_client {
	char  *name;
	u8 devices_id;
	void (*add)   (struct ipps_device *);
	void (*remove)(struct ipps_device *);

	struct list_head list;
};

struct ipps_device *ipps_alloc_device(size_t size);
void ipps_dealloc_device(struct ipps_device *device);
int ipps_register_device(struct ipps_device *device);
void ipps_unregister_device(struct ipps_device *device);
int ipps_register_client(struct ipps_client *client);
void ipps_unregister_client(struct ipps_client *client);

int ipps_command(struct ipps_client *client, enum ipps_cmd_type cmd,
			unsigned int object, void *data);
int ipps_get_freqs_table(struct ipps_client *client, unsigned int object,
			struct cpufreq_frequency_table *table);
int ipps_get_current_freq(struct ipps_client *client, unsigned int object,
			unsigned int *freq);
int ipps_set_current_freq(struct ipps_client *client, unsigned int object,
			unsigned int *freq);
int ipps_get_current_policy(struct ipps_client *client, unsigned int object,
			unsigned int *policy);
int ipps_set_current_policy(struct ipps_client *client, unsigned int object,
			unsigned int *policy);
int ipps_get_parameter(struct ipps_client *client, unsigned int object,
			struct ipps_param *param);
int ipps_set_parameter(struct ipps_client *client, unsigned int object,
			struct ipps_param *param);
int ipps_get_mode(struct ipps_client *client, unsigned int object,
			unsigned int *mode);
int ipps_set_mode(struct ipps_client *client, unsigned int object,
			unsigned int *mode);

#endif /* IPPS_H */
