/*
 * FileName: kernel/drivers/mtd/nve.c
 * Description: complement NV partition(or called block) read and write
 * in kernel.
 * Copyright (C) Hisilicon technologies Co., Ltd All rights reserved.
 * Revision history:
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nve.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>

static struct semaphore nv_sem;
static struct class *nve_class;
static struct NVE_struct *my_nve;

/*
 * Function name:nve_notify_add.
 * Discription:add NV notifier and create NV device node.
 * in NV read and write algorithms.
 */
static void nve_notify_add(struct mtd_info *mtd)
{
	if (!mtd)
		return;

	if (NULL == my_nve)
		return;

	/*Maybe exists many mtds,but mmcblk0 is what we needed*/
	if (strcmp(mtd->name, NV_DEVICE_NAME))
		return;

	printk(KERN_INFO "[NVE][%s]:nve_major_number:%d, mtd->index:%d.\n", __func__, my_nve->nve_major_number, mtd->index);
	printk(KERN_INFO "[NVE][%s]:mtd->name:%s.\n", __func__, mtd->name);

	/*create device inode automaticlly according to class name and major number*/
	device_create(nve_class, NULL, MKDEV(my_nve->nve_major_number, mtd->index*2),
	              NULL, "nve%d", mtd->index);

	return;
}

/*
 * Function name:nve_notify_remove.
 * Discription:remove NV notifier and destory NV device node.
 * in NV read and write algorithms.
 */
static void nve_notify_remove(struct mtd_info *mtd)
{
	if (!mtd)
		return;

	if (NULL == my_nve)
		return;

	device_destroy(nve_class, MKDEV(my_nve->nve_major_number, mtd->index*2));

	return;
}

/*define NV notifier and hook nve_notify_add and nve_notify_remove to it.*/
static struct mtd_notifier nve_notifier = {
	.add    = nve_notify_add,
	.remove = nve_notify_remove,
};

/*
 * Function name:nve_increment.
 * Discription:complement NV block' increment automatically, when current block
 * has been writeen, block index will pointer to next block, and if current block
 * is maximum block count ,then block index will be assigned "1", ensuring all of
 * NV block will be used and written circularly.
 */
static void nve_increment(struct NVE_struct *nve)
{
	if (nve->nve_current_id >= nve->nve_partition_count - 1)
		nve->nve_current_id = 1;
	else
		nve->nve_current_id++;

	return;
}

/*
 * Function name:nve_read.
 * Discription:read NV partition.
 * Parameters:
 *          @ mtd:struct mtd_info pointer.
 *          @ from:emmc start block number that will be read.
 *          @ len:total bytes that will be read from emmc.
 *          @ buf:buffer used to store bytes that is read from emmc.
 */
static int nve_read(struct mtd_info *mtd, loff_t from, size_t len, u_char *buf)
{
	int ret = 0;
	size_t retlen;

	ret = mtd->read(mtd, from, len, &retlen, buf);
	if ((ret && (ret != -EUCLEAN) && (ret != -EBADMSG)) || (retlen != len)) {
		printk(KERN_ERR "[NVE][%s] Fatal error, read flash from = 0x%x, len = 0x%x, retlen = 0x%x, ret = 0x%x.\n",
		       __func__, (uint32_t)from, len, retlen, ret);
		ret = -ENODEV;
	} else
		ret = 0;

	return ret;
}

/*
 * Function name:nve_erase.
 * Discription:erase NV partition by means of writing "0" to pointed emmc block.
 * Parameters:
 *          @ mtd:struct mtd_info pointer.
 *          @ from:emmc start block number that will be erased.
 *          @ len:total bytes that will be erased from emmc.
 */
static int nve_erase(struct mtd_info *mtd, loff_t from, size_t len)
{
	int ret = 0;
	struct erase_info erase;

	erase.addr = from;
	erase.len = len;
	erase.mtd = mtd;
	erase.callback = NULL;
	erase.priv = 0;

	ret = mtd->erase(mtd, &erase);
	if (ret) {
		printk(KERN_ERR "[NVE][%s] fatal error, erase flash from = 0x%x, len = 0x%x, ret = 0x%x.\n",
		       __func__, (uint32_t)from, len, ret);

		ret = -ENODEV;
	}

	return ret;
}

/*
 * Function name:nve_write.
 * Discription:write NV partition.
 * Parameters:
 *          @ mtd:struct mtd_info pointer.
 *          @ from:emmc start block number that will be written.
 *          @ len:total bytes that will be written from emmc.
 *          @ buf:given buffer whose bytes will be written to emmc.
 */
static int nve_write(struct mtd_info *mtd, loff_t from, size_t len, u_char *buf)
{
	int ret = 0;
	size_t retlen;

	ret = nve_erase(mtd, from, len);
	if (!ret) {
		ret = mtd->write(mtd, from, len, &retlen, buf);
		if ((ret) || (retlen != len)) {
			printk(KERN_ERR "[NVE][%s] fatal error, write flash from = 0x%x, len = 0x%x, retlen = 0x%x, ret = 0x%x.\n",
			       __func__, (uint32_t)from, len, retlen, ret);

			ret = -ENODEV;
		}
	}
	mtd->sync(mtd);

	return ret;
}

/*
 * Function name:nve_check_partition.
 * Discription:check current NV partition is valid partition or not by means of
 * comparing current partition's name to NVE_HEADER_NAME.
 * Parameters:
 *          @ nve:struct NVE_struct pointer.
 *          @ index:indicates current NV partion that will be checked.
 * return value:
 *          @ 0 - current parition is valid.
 *          @ others - current parition is invalid.
 */
static int nve_check_partition(struct NVE_struct *nve, uint32_t index)
{
	int ret = 0;
	struct mtd_info *mtd = nve->mtd;
	struct NVE_partition_header *nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);

	ret = nve_read(mtd, index * NVE_PARTITION_SIZE, NVE_PARTITION_SIZE, nve->nve_ramdisk);

	if (ret) {
		printk(KERN_ERR "[NVE][%s]nve_read error in line %d!\n", __func__, __LINE__);
		return ret;
	}

	/*compare nve_partition_name with const name to decide current partition is valid or not*/
	ret = strncmp(NVE_HEADER_NAME, nve_header->nve_partition_name, strlen(NVE_HEADER_NAME));

	return ret;
}

/*
 * Function name:nve_find_valid_partition.
 * Discription:find valid NV partition in terms of checking every
 * partition circularly. when two or more NV paritions are both valid,
 * nve_age will be used to indicates which one is really valid, i.e. the
 * partition whose age is the biggest is valid partition.
 */
static void nve_find_valid_partition(struct NVE_struct *nve)
{
	uint32_t i = 0;
	uint32_t age_temp = 0;
	int partition_valid = 0;

	struct NVE_partition_header *nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);

	for (i = 1; i < nve->nve_partition_count; i++) {
		partition_valid = nve_check_partition(nve, i);

		if (partition_valid)
			continue;

		if (nve_header->nve_age > age_temp) {
			nve->nve_current_id = i;
			age_temp = nve_header->nve_age;
		}
	}

	printk(KERN_DEBUG "[NVE][%s]current_id = %d.\n", __func__, nve->nve_current_id);

	return;
}

/*
 * Function name:nve_restore.
 * Discription:NV is divided into 8 partitions(partition0 - parititon 7),
 * when we need to add new NV items, we should update partition0 first,
 * and then restore parition0 to current valid partition which shall be
 * one of partition0 - partition7.
 * Parameters:
 *          @ 0  - success
 *          @ -1 - failure
 */
static int nve_restore(struct NVE_struct *nve)
{
	int ret = 0;
	uint32_t i;
	uint32_t valid_items;
	uint32_t id = nve->nve_current_id;
	u_char *nve_ramdisk_temp = NULL;
	struct mtd_info *mtd = nve->mtd;
	struct NVE_partition_header *nve_header;
	struct NVE_partition_header *nve_header_temp;
	struct NV_struct *nv;
	struct NV_struct *nv_temp;

	if (NVE_INVALID_NVM == id) {
		if (nve_read(mtd, 0, NVE_PARTITION_SIZE, (u_char *)nve->nve_ramdisk)) {
			printk(KERN_ERR "[NVE][%s] nve read error in line [%d].\n", __func__, __LINE__);
			return -ENODEV;
		}
		id = 1;
	} else {
		nve_ramdisk_temp = (u_char *)kzalloc(NVE_PARTITION_SIZE, GFP_ATOMIC);

		if (NULL == nve_ramdisk_temp) {
			printk(KERN_ERR "[NVE][%s]failed to allocate ramdisk temp buffer.\n", __func__);
			return -EFAULT;
		} else {
			if (nve_read(mtd, id * NVE_PARTITION_SIZE, NVE_PARTITION_SIZE, (u_char *)nve->nve_ramdisk)) {
				printk(KERN_ERR "[NVE][%s] nve read error in line [%d].\n", __func__, __LINE__);
				kfree(nve_ramdisk_temp);
				return -EFAULT;
			}

			if (nve_read(mtd, 0, NVE_PARTITION_SIZE, (u_char *)nve_ramdisk_temp)) {
				printk(KERN_ERR "[NVE][%s] nve read error in line [%d].\n", __func__, __LINE__);
				kfree(nve_ramdisk_temp);
				return -EFAULT;
			}

			nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);
			nve_header_temp = (struct NVE_partition_header *)(nve_ramdisk_temp + PARTITION_HEADER_OFFSET);

			nv = (struct NV_struct *)nve->nve_ramdisk;
			nv_temp = (struct NV_struct *)nve_ramdisk_temp;

			valid_items = min(nve_header->valid_items, nve_header_temp->valid_items);

			for (i = 0; i < valid_items; i++, nv++, nv_temp++) {
				/*skip non-volatile NV item*/
				if (nv->nv_header.nv_property)
					continue;

				memcpy((void *)nv, (void *)nv_temp, sizeof(struct NV_struct));
			}

			/* new added item need not check nv_property, copy directly */
			if (nve_header_temp->valid_items > nve_header->valid_items) {
				memcpy((void *)nv, (void *)nv_temp, sizeof(struct NV_struct) * (nve_header_temp->valid_items - nve_header->valid_items));
				nve_header->valid_items = nve_header_temp->valid_items;
			}

			nve_header->nve_age++;
			nve_header->nve_version = nve_header_temp->nve_version;
		}

		if (id == nve->nve_partition_count - 1)
			id = 1;
		else
			id++;
	}

	ret = nve_write(mtd, id * NVE_PARTITION_SIZE, NVE_PARTITION_SIZE, (u_char *)nve->nve_ramdisk);

	if (ret) {
		printk(KERN_ERR "[NVE][%s]nve_write error in line %d!\n", __func__, __LINE__);
		return ret;
	}

	/*ensure write success,then erase partition 0*/
	nve_erase(mtd, 0, NVE_PARTITION_SIZE);
	nve_increment(nve);

	kfree(nve_ramdisk_temp);
	return ret;
}

/*
 * Function name:nve_do_index_table.
 * Discription:create NV index table. Once NV index table has been created, we can
 * visit NV items by means of this nv_index_table conveniently.
 * Parameters:
 *          @ nve:struct NVE_struct pointer.
 * return value:
 *          @ 0 - success.
 *          @ -1- failure.
 */
static int nve_do_index_table(struct NVE_struct *nve)
{
	u_char *p_nv;
	unsigned int i;
	unsigned int nv_offset;
	unsigned int nv_offset_next;
	struct NVE_index *nv_index;
	struct NV_header *nv;
	struct mtd_info *mtd = nve->mtd;
	struct NVE_partition_header *nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);

	if (nve_read(mtd, nve->nve_current_id * NVE_PARTITION_SIZE, NVE_PARTITION_SIZE, (u_char *)nve->nve_ramdisk)) {
		printk(KERN_ERR "[NVE][%s] nve read error in line [%d]!\n", __func__, __LINE__);
		return -ENODEV;
	}

	printk(KERN_DEBUG "[NVE]Original valid_items: 0x%x.\n", nve_header->valid_items);

	if (NULL != nve->nve_index_table) {
		kfree(nve->nve_index_table);
		nve->nve_index_table = NULL;
	}
	nve->nve_index_table = kzalloc(nve_header->valid_items * sizeof(struct NVE_index), GFP_ATOMIC);

	if (NULL == nve->nve_index_table) {
		printk(KERN_ERR "[NVE] nve_do_index_table failed to allocate index table.\n");
		return -ENOMEM;
	}

	nv_offset = 0;
	for (i = 0; i < nve_header->valid_items; i++) {
		nv_offset_next = nv_offset + sizeof(struct NV_header);

		/*override, the end.Last 128 bytes is reserverd for nve_partition_header.*/
		if (nv_offset_next > NVE_PARTITION_SIZE - PARTITION_HEADER_SIZE)
			break;

		p_nv = nve->nve_ramdisk + nv_offset;
		nv = (struct NV_header *)p_nv;

		if (i != nv->nv_number)
			/*invalid nv, the end*/
			break;

		nv_offset_next += NVE_NV_DATA_SIZE;
		/*override, the end.Last 128 bytes is reserverd for nve_partition_header.*/
		if (nv_offset_next > NVE_PARTITION_SIZE - PARTITION_HEADER_SIZE)
			break;

		nv_index = nve->nve_index_table + i;
		nv_index->nv_offset = nv_offset;
		nv_index->nv_size = nv->valid_size;
		memcpy(nv_index->nv_name, nv->nv_name, NV_NAME_LENGTH);

		nv_offset = nv_offset_next;
	}
	nve_header->valid_items = i;

	printk(KERN_DEBUG "[NVE]Actual valid_items: 0x%x.\n", nve_header->valid_items);

	return 0;
}

/* test NV items in kernel. if you want to use this, please set macro
 * TEST_NV_IN_KERNEL to "1".
 */
#if TEST_NV_IN_KERNEL
static int testnve(struct NVE_struct *nve)
{
	struct NVE_partition_header *nve_header;
	struct NV_struct *nv;
	uint32_t valid_items;
	uint32_t i;

	nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);
	nv = (struct NV_struct *)nve->nve_ramdisk;
	valid_items = nve_header->valid_items;

	printk(KERN_INFO "[NVE] testnve: version %d.\n", nve_header->nve_version);
	printk(KERN_INFO "[NVE] testnve: age %d.\n", nve_header->nve_age);
	printk(KERN_INFO "[NVE] testnve: valid_items %d.\n", valid_items);

	for (i = 0; i < valid_items; i++) {
		printk(KERN_INFO "[NVE] testnve: number %d, name %s, NV %d, size %d.\n", nv->nv_header.nv_number,
				nv->nv_header.nv_name,
				nv->nv_header.nv_property,
				nv->nv_header.valid_size);
		printk(KERN_INFO "[NVE] testnve: data 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x .\n\n", nv->nv_data[0],
				nv->nv_data[1],
				nv->nv_data[2],
				nv->nv_data[3],
				nv->nv_data[4],
				nv->nv_data[5]);
		nv++;
	}

	return 0;
}
#endif

/*
 * Function name:nve_open_ex.
 * Discription:open NV device.
 * return value:
 *          @ 0 - success.
 *          @ -1- failure.
 */
static int nve_open_ex(void)
{
	int ret = 0;
	struct mtd_info *mtd;
	struct NVE_struct *nve;

	/*use the semaphore to ensure that only one thread can visit the device at the same time*/
	if (down_interruptible(&nv_sem))
		return -EBUSY;

	/*the driver is not initialized successfully, return error*/
	if (NULL == my_nve) {
		ret = -ENODEV;
		printk(KERN_ERR "[NVE][%s]:driver is not initialized successfully.\n", __func__);
		goto out;
	} else
		nve = my_nve;

	/*if NV has been initiallized,then skip following code in this function*/
	if (nve->initialized > 0) {
		printk(KERN_INFO "[NVE][%s]:NV has been initialized!\n", __func__);
		goto out;
	}

	/*Get mtd handle according device's name*/
	mtd = get_mtd_device_nm(NV_DEVICE_NAME);

	if (IS_ERR(mtd)) {
		printk(KERN_ERR "[NVE][%s]: get_mtd_device_nm error.\n", __func__);
		ret = PTR_ERR(mtd);
		goto out;
	}

	if (MTD_ABSENT == mtd->type) {
		put_mtd_device(mtd);
		ret = -ENODEV;
		goto out;
	}

	nve->mtd = mtd;

	/*Total avaliable NV partition size is 4M,but we only use 1M*/
	nve->nve_partition_count = (mtd->size / NVE_PARTITION_SIZE) / 4;

	nve->nve_current_id = NVE_INVALID_NVM;
	printk(KERN_INFO "[NVE] nve->nve_partition_count = 0x%x.\n", nve->nve_partition_count);

	/*
	 * partition count must bigger than 3,
	 * one for partition 0,one for update, the other for runtime.
	 */
	if (nve->nve_partition_count <= 3) {
		ret = -ENODEV;
		goto out;
	}

	nve_find_valid_partition(nve);

	/*check partiton 0 is valid or not*/
	ret = nve_check_partition(nve, 0);

	if (!ret) {
		/*partiton 0 is valid, restore it to current partition*/
		printk(KERN_INFO "[NVE]partition0 is valid, restore it to current partition.\n");
		ret = nve_restore(nve);
	}

	if (ret) {
		if (NVE_INVALID_NVM == nve->nve_current_id) {
			printk(KERN_ERR "[NVE] nve_open_ex: no valid NVM.\n");
			ret = -ENODEV;
			goto out;
		} else
			ret = 0;
	}

	/*
	 *create NV index table,so we can visit any NV items
	 *that we want to read or write conveniently.
	 */
	if (nve_do_index_table(nve)) {
		ret = -ENODEV;
		goto out;
	}

	printk(KERN_INFO "[NVE][%s]current_id = %d\n", __func__, nve->nve_current_id);

	nve->initialized = 1;

out:
	/*release the semaphore*/
	up(&nv_sem);
	return ret;
}

/*
 * Function name:nve_direct_access.
 * Discription:read or write NV items interfaces that will be called by other functions.
 * Parameters:
 *          @ user_info:struct nve_info_user pointer.
 * return value:
 *          @ 0 - success.
 *          @ -1- failure.
 */
int nve_direct_access(struct nve_info_user *user_info)
{
	int ret = 0;
	u_char *nv_data = NULL;
	struct mtd_info *mtd = NULL;
	struct NVE_struct *nve = NULL;
	struct NVE_index *nve_index = NULL;
	struct NV_header *nv = NULL;
	struct NVE_partition_header *nve_header = NULL;

	/*input parameter invalid, return.*/
	if (NULL == user_info) {
		ret = -ENODEV;
		goto out;
	}

	/*open nve.*/
	ret = nve_open_ex();
	if (ret)
		goto out;

	/*ensure only one process can visit NV device at the same time in kernel*/
	if (down_interruptible(&nv_sem))
		return -EBUSY;

	nve = my_nve;

	mtd = nve->mtd;
	nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);

	printk(KERN_INFO "[NVE][%s]valid items 0x%x\n", __func__, nve_header->valid_items);

	/*check NV number is valid or not*/
	if (user_info->nv_number >= nve_header->valid_items) {
		printk(KERN_ERR "[NVE][%s] NV items[%d] is not defined.\n", __func__, user_info->nv_number);
		up(&nv_sem);
		return -EFAULT;
	}

	/*Find the NV position in NV index table*/
	nve_index = nve->nve_index_table + user_info->nv_number;

	/*check NV length is valid or not*/
	if (user_info->valid_size > NVE_NV_DATA_SIZE) {
		printk(KERN_WARNING "[NVE]Bad parameter,inputed NV length is bigger than 104,the surplus will be neglected\n");
		user_info->valid_size = NVE_NV_DATA_SIZE;
	} else if (0 == user_info->valid_size) {
		user_info->valid_size = nve_index->nv_size;
		printk(KERN_WARNING "[NVE]Bad parameter,inputed NV length is 0,will be reassigned according to NV inedx table\n");
	}

	nv_data = nve->nve_ramdisk + nve_index->nv_offset + sizeof(struct NV_header);

	if (NV_READ == user_info->nv_operation) {
		/*read nv from ramdisk*/
		memcpy(user_info->nv_data, nv_data, user_info->valid_size);
	} else {
		/*write nv to ramdisk*/
		nv = (struct NV_header *)(nve->nve_ramdisk + nve_index->nv_offset);
		nv->valid_size = user_info->valid_size;
		nve_index->nv_size = user_info->valid_size;
		memset(nv_data, 0x0, NVE_NV_DATA_SIZE);
		memcpy(nv_data, user_info->nv_data, user_info->valid_size);

		/*update nve_ramdisk*/
		nve_header->nve_age++;

		/*write NV to emmc*/
		nve_increment(nve);
		ret = nve_write(mtd, nve->nve_current_id * NVE_PARTITION_SIZE, NVE_PARTITION_SIZE, (u_char *)nve->nve_ramdisk);
	}

	/*release the semaphore*/
	up(&nv_sem);
out:
	return ret;
}

/*
 * Function name:nve_open.
 * Discription:open NV device in terms of calling nve_open_ex().
 * return value:
 *          @ 0 - success.
 *          @ -1- failure.
 */
static int nve_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	ret = nve_open_ex();
	return ret;
}


static int nve_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 * Function name:nve_ioctl.
 * Discription:complement read or write NV by terms of sending command-words.
 * return value:
 *          @ 0 - success.
 *          @ -1- failure.
 */
static long nve_ioctl(struct file *file, u_int cmd, u_long arg)
{
	int ret = 0;
	struct mtd_info *mtd = NULL;
	void __user *argp = (void __user *)arg;
	u_long size;
	struct nve_info_user info;
	struct NVE_struct *nve = NULL;
	struct NVE_index *nve_index = NULL;
	struct NV_header *nv = NULL;
	struct NVE_partition_header *nve_header = NULL;
	u_char *nv_data = NULL;

	/*ensure only one process can visit NV device at the same time in API*/
	if(down_interruptible(&nv_sem))
		return -EBUSY;

	if (NULL == my_nve) {
		printk(KERN_ERR "[NVE][%s]my_nve not initialized!\n", __func__);
		up(&nv_sem);
		return -EFAULT;
	}

	nve = my_nve;

	mtd = nve->mtd;
	size = ((cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT);

	if (cmd & IOC_IN) {
		if (!access_ok(VERIFY_READ, argp, size)) {
			printk(KERN_ERR "[NVE][%s]access_in error!\n", __func__);
			up(&nv_sem);
			return -EFAULT;
		}
	}

	if (cmd & IOC_OUT) {
		if (!access_ok(VERIFY_WRITE, argp, size)) {
			printk(KERN_ERR "[NVE][%s]access_out error!\n", __func__);
			up(&nv_sem);
			return -EFAULT;
		}
	}

	switch (cmd) {
		case NVEACCESSDATA:
			if (copy_from_user(&info, argp, sizeof(struct nve_info_user))) {
				up(&nv_sem);
				return -EFAULT;
			}
	               printk("[NVE][%s]current_id = %d,ioctl = %d.\n", __func__, nve->nve_current_id,info.nv_operation);
                       if(info.nv_operation)
                       {
                           if (nve_do_index_table(nve))
                           {
                              printk("[NVE]nve_ioctl,read nv from emmc error,nve_current_id = %d.\n",nve->nve_current_id);
                              return -EFAULT;
                           }
                       }

			nve_header = (struct NVE_partition_header *)(nve->nve_ramdisk + PARTITION_HEADER_OFFSET);

			if (info.nv_number >= nve_header->valid_items) {
				printk(KERN_ERR "[NVE][%s], nv[%d] is not defined.\n", __func__, info.nv_number);
				up(&nv_sem);
				return -EFAULT;
			}

			nve_index = nve->nve_index_table + info.nv_number;

			if (info.valid_size > NVE_NV_DATA_SIZE) {
				printk(KERN_WARNING "[NVE]Bad parameter,inputed NV length is bigger than 104,the surplus will be neglected\n");
				info.valid_size = NVE_NV_DATA_SIZE;
			} else if (info.valid_size == 0) {
				printk(KERN_WARNING "[NVE]Bad parameter,inputed NV length is 0,will be reassigned according to NV inedx table\n");
				info.valid_size = nve_index->nv_size;
			}

			nv_data = nve->nve_ramdisk + nve_index->nv_offset + sizeof(struct NV_header);

			if (NV_READ == info.nv_operation) {
				/*read nv from ramdisk*/
				memcpy(info.nv_data, nv_data, info.valid_size);

				/*send back to user*/
				if (copy_to_user(argp, &info, sizeof(struct nve_info_user))) {
					up(&nv_sem);
					return -EFAULT;
				}
			} else {
				/*write nv to ramdisk*/
				nv = (struct NV_header *)(nve->nve_ramdisk + nve_index->nv_offset);
				nv->valid_size = info.valid_size;
				nve_index->nv_size = info.valid_size;
				memset(nv_data, 0x0, NVE_NV_DATA_SIZE);
				memcpy(nv_data, info.nv_data, info.valid_size);

				/*update nve_ramdisk*/
				nve_header->nve_age++;

				/*write NVE to emmc*/
				nve_increment(nve);
				ret = nve_write(mtd, nve->nve_current_id * NVE_PARTITION_SIZE, NVE_PARTITION_SIZE, (u_char *)nve->nve_ramdisk);
			}
			break;
		default:
			printk(KERN_ERR "[NVE][%s] Unknow command!\n", __func__);
			ret = -ENOTTY;
			break;
	}

	up(&nv_sem);
	return ret;
}

static const struct file_operations nve_fops = {
	.owner             = THIS_MODULE,
	.unlocked_ioctl    = nve_ioctl,
	.open              = nve_open,
	.release           = nve_close,
};

static int __init init_nve(void)
{
	int error;
	struct NVE_struct *nve = NULL;

	/*semaphore initial*/
	sema_init(&nv_sem, 1);

	my_nve = nve = kzalloc(sizeof(struct NVE_struct), GFP_ATOMIC);

	if (nve == NULL) {
		printk(KERN_ERR "[NVE][%s]failed to allocate driver data in line %d.\n", __func__, __LINE__);
		return -ENOMEM;
	}

	memset (nve, 0x0, sizeof(struct NVE_struct));
	nve->nve_ramdisk= (u_char *)kzalloc(NVE_PARTITION_SIZE, GFP_ATOMIC);

	if (NULL == nve->nve_ramdisk) {
		printk(KERN_ERR "[NVE][%s]failed to allocate ramdisk buffer in line %d.\n", __func__, __LINE__);
		error = -ENOMEM;
		goto failed_free_driver_data;
	}

	nve->nve_major_number = register_chrdev(0, "nve", &nve_fops);

	if (nve->nve_major_number < 0) {
		printk(KERN_NOTICE "[NVE]Can't allocate major number for Non-Volatile memory Extension device.\n");
		error = -EAGAIN;
		goto failed_free_ramdisk;
	}

	nve_class = class_create(THIS_MODULE, "nve");

	if (IS_ERR(nve_class)) {
		printk(KERN_ERR "[NVE]Error creating nve class.\n");
		unregister_chrdev(nve->nve_major_number, "nve");
		error = PTR_ERR(nve_class);
		goto failed_free_ramdisk;
	}

	register_mtd_user(&nve_notifier);
	printk(KERN_INFO "[NVE][%s]NV init ok!\n", __func__);

	return 0;

failed_free_ramdisk:
	kfree(nve->nve_ramdisk);
	nve->nve_ramdisk = NULL;
failed_free_driver_data:
	kfree(nve);
	my_nve = NULL;
	return error;
}

static void __exit cleanup_nve(void)
{
	unregister_mtd_user(&nve_notifier);
	class_destroy(nve_class);
	if (NULL != my_nve) {
		unregister_chrdev(my_nve->nve_major_number, "nve");
		kfree(my_nve->nve_index_table);
		my_nve->nve_index_table = NULL;
		kfree(my_nve->nve_ramdisk);
		my_nve->nve_ramdisk = NULL;
		kfree(my_nve);
		my_nve = NULL;
	}

	return;
}

module_init(init_nve);
module_exit(cleanup_nve);

/*export nve_direct_access,so we can use it in other procedures*/
EXPORT_SYMBOL(nve_direct_access);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hisi-k3v200");
MODULE_DESCRIPTION("Direct character-device access to NVE devices");
