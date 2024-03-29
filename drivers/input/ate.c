/*
 *  ate.c - Linux kernel modules for huawei automatic test engine
 *
 *  Copyright (C) 2012 sunhonghui <sunhonghui@huawei.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/stat.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/usb/otg.h>

//#define MSM_8960_PLATFORM
//#define TI_OMAP_PLATFORM
#define HISI_K3_PLATFORM

#ifdef MSM_8960_PLATFORM
#include <linux/mfd/pm8xxx/pm8921-charger.h>
#endif
#ifdef HISI_K3_PLATFORM
#include <linux/usb/hiusb_android.h>
#endif

#define ATE_IEDT_SIZE           128 * 1024
#define IE_SIZE                 16
#define ATE_IEDT_HEAD_SIZE      2
#define MAX_SIZE_OF_DEV_NAME    64

#define MAX_INPUT_DEV           32
#define MAX_FILE_DIR_NAME       512

//#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define ATE_DEBUG
#define INPUT_EVENTS_DATA_FILE_DEFAULT "/data/hwate_record"
#define HWATE_CONF_FILE "/data/hwate.conf"
#define TOUCH_EVENT				0
#define TOUCH_BUTTON_EVENT		1
#define POWER_KEY_EVENT			2
#define SOUND_KEY_EVENT			3
#define MAX_EVENT_TYPE			10
#define NULL_EVENT_TYPE			-1
#define CONNECTED               1
#define MAX_VIRTUALKEYS			4
#define MAX_VIRTUAL_SCREEN_SIZE		4096

#define  COHERENCE_FLATFORM

static struct ate_data *ate_dt = NULL;
static struct kobject *hw_ate_kobj = NULL;

struct auto_test_dev {
	struct input_dev *input_dev_table[MAX_INPUT_DEV];
	bool valid[MAX_INPUT_DEV];
	int input_event_type[MAX_INPUT_DEV];
	unsigned int input_dev_sum;
};

struct input_event_data {
    unsigned long jiffies_t;
    unsigned int code;
    int value;
    unsigned short type;
    unsigned short dev_no;
};

struct input_dev_filter {
    char dev_name[MAX_INPUT_DEV][MAX_SIZE_OF_DEV_NAME];
    int input_event_type[MAX_INPUT_DEV];
    unsigned int filer_total;
};

struct local {
int x;
int y;
int w;
int h;
};

struct virtualkey_dev {
unsigned int type;
unsigned int code;
struct local l;
};

struct virtualkey {
struct virtualkey_dev virtualkey_device[MAX_VIRTUALKEYS];
unsigned int total_dev;
};

struct virtualbutton {
struct virtualkey_dev vlkey_dev;
unsigned long first_t;
unsigned long end_t;
unsigned long first_step;
unsigned long end_step;
int flag;
};

struct ate_data {
	unsigned long jiffies_frist_record;
	unsigned long jiffies_end_record;
	unsigned long jiffies_frist_playback;
	unsigned long jiffies_cur_playback;
	unsigned long playback_delay_ms;
	char iedt_file_dir[MAX_FILE_DIR_NAME];
	unsigned long ie_times;
	unsigned long is_record;
	int has_virtualkey;
	unsigned long playback_continuance_ms;
	unsigned long cur_step;
	unsigned int has_usb_disconnect;
#if defined MSM_8960_PLATFORM || defined HISI_K3_PLATFORM
	unsigned int usb_status;
#endif
	long pwrkey_step;
	long pwrkey_jiffies_end_record;
	struct input_dev_filter idf;
	struct auto_test_dev ate_dev;
	struct delayed_work playback_work;
	struct input_event_data  *ie_dt;
	struct wake_lock wake_lock;
	struct virtualkey virtualkey;
	struct virtualbutton vlbutton;
#ifdef HISI_K3_PLATFORM
	struct notifier_block nb;
#endif
};

extern char buf_virtualkey[500];
extern ssize_t  buf_vkey_size;
static void store_input_event_data(unsigned short dev_no, unsigned short type, unsigned int code, int value);
static void set_input_device_filter_array(void);
static void store_touchbutton_event_data(unsigned short dev_no, unsigned short type, unsigned int code, int value,unsigned long jiffies_temp);

static int virtualbutton_to_realbutton(void)
{
    int i = 0;
    int j = 0;
    struct input_event_data *ie_data_n = NULL;
    struct input_event_data *ie_data_p = NULL;

#ifdef ATE_DEBUG
    printk("%lu,%lu,%lu,%lu,%d\n",ate_dt->vlbutton.end_step,ate_dt->vlbutton.end_t,ate_dt->vlbutton.first_step,
        ate_dt->vlbutton.first_t,ate_dt->vlbutton.flag);
    printk("%d,%d,%d,%d,%d,%d,\n",ate_dt->vlbutton.vlkey_dev.code,ate_dt->vlbutton.vlkey_dev.type,ate_dt->vlbutton.vlkey_dev.l.h,
        ate_dt->vlbutton.vlkey_dev.l.w,ate_dt->vlbutton.vlkey_dev.l.x,ate_dt->vlbutton.vlkey_dev.l.y);
#endif

    for(i = 0; i < ate_dt->virtualkey.total_dev; i++) {
        if(ate_dt->vlbutton.vlkey_dev.l.x > (ate_dt->virtualkey.virtualkey_device[i].l.x - ate_dt->virtualkey.virtualkey_device[i].l.w/2) &&
                ate_dt->vlbutton.vlkey_dev.l.x < (ate_dt->virtualkey.virtualkey_device[i].l.x + ate_dt->virtualkey.virtualkey_device[i].l.w/2) &&
                ate_dt->vlbutton.vlkey_dev.l.y > (ate_dt->virtualkey.virtualkey_device[i].l.y - ate_dt->virtualkey.virtualkey_device[i].l.h/2) &&
                ate_dt->vlbutton.vlkey_dev.l.y < (ate_dt->virtualkey.virtualkey_device[i].l.y + ate_dt->virtualkey.virtualkey_device[i].l.h/2)) {
                    break;
        }
    }

    if(i >= ate_dt->virtualkey.total_dev) {
        printk("i = %d no such key!\n",i);
        return -1;
    }

#ifdef ATE_DEBUG
    printk("type = %d,code = %d\n",ate_dt->virtualkey.virtualkey_device[i].code,
        ate_dt->virtualkey.virtualkey_device[i].type);
#endif

    if(ate_dt->ie_times > 1) {
        j = ate_dt->ie_times - 1;
        ie_data_n = ate_dt->ie_dt + j;
        ie_data_p = ate_dt->ie_dt + j - 1;

        ate_dt->vlbutton.end_t = ie_data_n->jiffies_t;

        while(1) {
#ifdef ATE_DEBUG
            printk("j=%d,ie_data_n->type=%d,ie_data_p->type=%d\n",j,ie_data_n->type,ie_data_p->type);
#endif
            if(j == 0 || (EV_SYN != ie_data_n->type && EV_SYN == ie_data_p->type)) {
                break;
            }
            j--;
            ie_data_n = ate_dt->ie_dt + j;
            ie_data_p = ate_dt->ie_dt + j - 1;
        }

        ate_dt->ie_times = j;
        store_touchbutton_event_data(TOUCH_BUTTON_EVENT,ate_dt->virtualkey.virtualkey_device[i].type,
            ate_dt->virtualkey.virtualkey_device[i].code,1,ie_data_n->jiffies_t);
        store_touchbutton_event_data(TOUCH_BUTTON_EVENT,EV_SYN,SYN_REPORT,0,ie_data_n->jiffies_t);
        store_touchbutton_event_data(TOUCH_BUTTON_EVENT,ate_dt->virtualkey.virtualkey_device[i].type,
            ate_dt->virtualkey.virtualkey_device[i].code,0,ate_dt->vlbutton.end_t);
        store_touchbutton_event_data(TOUCH_BUTTON_EVENT,EV_SYN,SYN_REPORT,0,ate_dt->vlbutton.end_t);
    }

	return 0;
}

static void ate_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
    unsigned short i;
    struct input_event_data *ie_data = NULL;
    struct input_dev *dev = NULL;
    int value_temp = value;
    unsigned int code_temp = code;
    unsigned int type_temp = type;

    if (0 == ate_dt->is_record) {
        return;
    }

#ifdef TI_OMAP_PLATFORM
    if(CONNECTED != get_plugin_device_status()) {
        ate_dt->has_usb_disconnect = true;
    }
#endif
#if defined MSM_8960_PLATFORM || defined HISI_K3_PLATFORM
    if(CONNECTED != ate_dt->usb_status) {
        ate_dt->has_usb_disconnect = true;
    }
#endif

    for (i = 0; i < ate_dt->ate_dev.input_dev_sum; i++) {
	dev = ate_dt->ate_dev.input_dev_table[i];
        if((true == ate_dt->ate_dev.valid[i]) && (0 == strcmp(handle->dev->name, dev->name))) {
#ifdef ATE_DEBUG
            printk(KERN_DEBUG "index=%d, name=%s\n", i, dev->name);
            printk(KERN_DEBUG "type=%d, code=%d, value=%d\n",type_temp,code_temp,value_temp);
#endif

            if(0 !=ate_dt->ie_times && true == ate_dt->has_usb_disconnect) {
                ie_data = ate_dt->ie_dt + ate_dt->ie_times - 1;
                    if(POWER_KEY_EVENT == ate_dt->ate_dev.input_event_type[i] && EV_SYN == type_temp &&
                        POWER_KEY_EVENT == ie_data->dev_no && EV_SYN !=  ie_data->type) {
                ate_dt->pwrkey_jiffies_end_record = jiffies;
                ate_dt->pwrkey_step = ate_dt->ie_times;
                }
            }
#ifdef COHERENCE_FLATFORM
            if(TOUCH_EVENT == ate_dt->ate_dev.input_event_type[i]) {
                if(EV_ABS == type_temp) {
                    if(ABS_MT_TOUCH_MAJOR == code_temp) {
#ifdef ATE_DEBUG
                        printk("code_temp=%d\n",code_temp);
#endif
                        ate_dt->vlbutton.vlkey_dev.l.w = value_temp;
                        ate_dt->vlbutton.first_step = ate_dt->ie_times;
                        ate_dt->vlbutton.first_t = jiffies;
                    } else if(ABS_MT_WIDTH_MAJOR== code_temp) {
#ifdef ATE_DEBUG
                        printk("code_temp=%d\n",code_temp);
#endif
                        ate_dt->vlbutton.vlkey_dev.l.h = value_temp;
                    }else if(ABS_MT_POSITION_X== code_temp) {
#ifdef ATE_DEBUG
                        printk("code_temp=%d\n",code_temp);
#endif
                        ate_dt->vlbutton.vlkey_dev.l.x = value_temp;
                        value_temp = MAX_VIRTUAL_SCREEN_SIZE*value_temp/(dev->absinfo[ABS_MT_POSITION_X].maximum);
                    } else if (ABS_MT_POSITION_Y== code_temp ) {
#ifdef ATE_DEBUG
			printk("code_temp=%d\n",code_temp);
#endif
                        if(value_temp <= dev->absinfo[ABS_MT_POSITION_Y].maximum) {
                            value_temp = MAX_VIRTUAL_SCREEN_SIZE*value_temp/(dev->absinfo[ABS_MT_POSITION_Y].maximum);
                        } else {
#ifdef ATE_DEBUG
                        printk("code_temp=%d\n",code_temp);
#endif
			    ate_dt->vlbutton.flag = true;
			    ate_dt->vlbutton.vlkey_dev.l.y = value_temp;
                        }
                    }
                } else if (EV_SYN == type_temp) {
                    if(true == ate_dt->vlbutton.flag) {
                        ate_dt->vlbutton.end_step = ate_dt->ie_times;
                        ate_dt->vlbutton.end_t = jiffies;
                    }
                }
            }

            if(0 != ate_dt->ie_times && true == ate_dt->has_virtualkey) {
                ie_data = ate_dt->ie_dt + ate_dt->ie_times - 1;
                if(TOUCH_EVENT != ate_dt->ate_dev.input_event_type[i] ||
				(EV_SYN == ie_data->type && EV_SYN != type_temp)) {
                    if(true == ate_dt->vlbutton.flag) {
                        ate_dt->vlbutton.flag = false;
                        virtualbutton_to_realbutton();
                    }
                }
        }
#endif
            store_input_event_data((unsigned short)ate_dt->ate_dev.input_event_type[i], (unsigned short)type_temp, code_temp, value_temp);
            if(1 == ate_dt->ie_times && true == ate_dt->has_usb_disconnect) {
                if(POWER_KEY_EVENT != ate_dt->ate_dev.input_event_type[i] || EV_SYN == type_temp ) {
                    ate_dt->ie_times = 0;
                }
            }
        }
    }
}

static int ate_connect(struct input_handler *handler, struct input_dev *dev,
			 const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "ate_evhub";

	error = input_register_handle(handle);
	if (error)
		goto err_free_handle;

	error = input_open_device(handle);
	if (error)
		goto err_unregister_handle;

	if (MAX_INPUT_DEV > ate_dt->ate_dev.input_dev_sum) {
		ate_dt->ate_dev.valid[ate_dt->ate_dev.input_dev_sum] = false;
		ate_dt->ate_dev.input_event_type[ate_dt->ate_dev.input_dev_sum] = NULL_EVENT_TYPE;
		ate_dt->ate_dev.input_dev_table[ate_dt->ate_dev.input_dev_sum++] = dev;


	}

	printk(KERN_DEBUG pr_fmt("Connected device: %s (%s at %s)\n"),
	       dev_name(&dev->dev),
	       dev->name ?: "unknown",
	       dev->phys ?: "unknown");
	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_handle:
	kfree(handle);
	return error;
}

static void ate_disconnect(struct input_handle *handle)
{
    int i, j;
	printk(KERN_DEBUG pr_fmt("Disconnected device: %s\n"),
	       dev_name(&handle->dev->dev));
    for (i = 0; i < ate_dt->ate_dev.input_dev_sum; i++) {
        if (0 == strcmp(handle->dev->name, ate_dt->ate_dev.input_dev_table[i]->name)) {
            for (j = i; j < ate_dt->ate_dev.input_dev_sum - 1; j++) {
				ate_dt->ate_dev.input_dev_table[j] = ate_dt->ate_dev.input_dev_table[j + 1];
				ate_dt->ate_dev.valid[j] = ate_dt->ate_dev.valid[j + 1];
				ate_dt->ate_dev.input_event_type[j] = ate_dt->ate_dev.input_event_type[j+1];
			}
			ate_dt->ate_dev.input_dev_table[j] = NULL;
			ate_dt->ate_dev.valid[j] = false;
			ate_dt->ate_dev.input_event_type[j] = NULL_EVENT_TYPE;
			ate_dt->ate_dev.input_dev_sum--;
			break;
	   	}
	}

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id ate_ids[] = {
	{ .driver_info = 1 },	/* Matches all devices */
	{ },			/* Terminating zero entry */
};

MODULE_DEVICE_TABLE(input, evbug_ids);

static struct input_handler ate_handler = {
	.event = ate_event,
	.connect = ate_connect,
	.disconnect = ate_disconnect,
	.name =	"ate_handler",
	.id_table =	ate_ids,
};

static void store_touchbutton_event_data(unsigned short dev_no, unsigned short type, unsigned int code, int value,unsigned long jiffies_temp)
{
    struct input_event_data *ie_data = NULL;

#ifdef ATE_DEBUG
    printk("ate_dt->ie_times=%lu\n",ate_dt->ie_times);
#endif
    if (ate_dt->ie_times < ATE_IEDT_SIZE/IE_SIZE - ATE_IEDT_HEAD_SIZE) {
        ie_data = ate_dt->ie_dt + ate_dt->ie_times;
        ie_data->dev_no = dev_no;
        ie_data->type = type;
        ie_data->code = code;
        ie_data->value = value;
    } else {
        printk(KERN_ERR "%s: too many input events\n", __func__);
        return;
    }

    if (0 == ate_dt->ie_times) {
        ate_dt->jiffies_frist_record = jiffies;
        ie_data->jiffies_t = 0;
    } else {
        ate_dt->jiffies_end_record = jiffies;
        ie_data->jiffies_t = jiffies_temp;
    }
    ate_dt->ie_times++;
}


static void store_input_event_data(unsigned short dev_no, unsigned short type, unsigned int code, int value)
{
    struct input_event_data *ie_data = NULL;
printk("ate_dt->ie_times=%lu\n",ate_dt->ie_times);
    if (ate_dt->ie_times < ATE_IEDT_SIZE/IE_SIZE - ATE_IEDT_HEAD_SIZE) {
        ie_data = ate_dt->ie_dt + ate_dt->ie_times;
        ie_data->dev_no = dev_no;
        ie_data->type = type;
        ie_data->code = code;
        ie_data->value = value;
    } else {
        printk(KERN_ERR "%s: too many input events\n", __func__);
        return;
    }

    if (0 == ate_dt->ie_times) {
        ate_dt->jiffies_frist_record = jiffies;
        ie_data->jiffies_t = 0;
    } else {
        ate_dt->jiffies_end_record = jiffies;
        ie_data->jiffies_t = ate_dt->jiffies_end_record - ate_dt->jiffies_frist_record;
    }
    ate_dt->ie_times++;
}

static int inputevent_data_save(void)
{
    char buf[IE_SIZE]={0};
    char *tp_data =(char *)ate_dt->ie_dt;
    int i;
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;

    fp = filp_open(ate_dt->iedt_file_dir, O_RDWR | O_CREAT, 0644);
    if (IS_ERR(fp)) {
        printk(KERN_ERR "create file error\n");
        return -1;
    }
    fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    sprintf(buf, "%lu", ate_dt->ie_times);
    vfs_write(fp, buf, sizeof(buf), &pos);
    memset(buf, 0, IE_SIZE);
    sprintf(buf, "%lu", ate_dt->playback_continuance_ms);
    vfs_write(fp, buf, sizeof(buf), &pos);

    for (i = 0; i < ate_dt->ie_times; i++) {
        memset(buf, 0, IE_SIZE);
        memcpy(buf, tp_data, sizeof(struct input_event_data));
        tp_data = tp_data + sizeof(struct input_event_data);
        vfs_write(fp, buf, IE_SIZE, &pos);
    }
    filp_close(fp, NULL);
    set_fs(fs);
    return 0;
}

static int inputevent_data_read(void)
{
    char buf[IE_SIZE] = {0};
    char *tp_data = (char *)ate_dt->ie_dt;
    int i;
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;
    int ret = 0;

    fp = filp_open(ate_dt->iedt_file_dir, O_RDONLY , 0644);
    if (IS_ERR(fp)) {
        ate_dt->ie_times = 0;
        printk(KERN_ERR "%s: open file %s failed\n", __func__, INPUT_EVENTS_DATA_FILE_DEFAULT);
        return -1;
    }
    fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_read(fp, buf, sizeof(buf), &pos);
    ret = strict_strtoul(buf, 0, &ate_dt->ie_times);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
	    goto exit;
    }

    memset(buf, 0, IE_SIZE);
    vfs_read(fp, buf, sizeof(buf), &pos);
    ret = strict_strtoul(buf, 0, &ate_dt->playback_continuance_ms);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
	    goto exit;
    }
    for (i = 0; i < ate_dt->ie_times; i++) {
        memset(buf, 0, IE_SIZE);
        vfs_read(fp, buf, sizeof(buf), &pos);
        memcpy(tp_data, buf, sizeof(struct input_event_data));
        tp_data += sizeof(struct input_event_data);
    }
exit:
    filp_close(fp, NULL);
    set_fs(fs);
    return ret;
}

static ssize_t show_inputevents_count(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%lu\n", ate_dt->ie_times);
}

static DEVICE_ATTR(inputevents_count, (S_IWUSR|S_IRUGO),
	show_inputevents_count, NULL);

static void del_input_event_data(void)
{

    if(true != ate_dt->has_usb_disconnect) {
        return ;
    }
    if(-1 == ate_dt->pwrkey_step) {
        return ;
    }
    ate_dt->ie_times = ate_dt->pwrkey_step + 1;
    ate_dt->jiffies_end_record= ate_dt->pwrkey_jiffies_end_record;
}
static ssize_t store_record_user_ops(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;

    rc = strict_strtoul(buf, 0, &ate_dt->is_record);
    if (rc)
        return rc;

    if (0 != ate_dt->is_record) {
	ate_dt->pwrkey_step = -1;
        ate_dt->ie_times = 0;
        ate_dt->has_usb_disconnect = false;
        if (0 == ate_dt->idf.filer_total) {
            set_input_device_filter_array();
        }
    } else if (ate_dt->ie_times > 0) {
    del_input_event_data();
        ate_dt->playback_continuance_ms =
            jiffies_to_msecs(ate_dt->jiffies_end_record - ate_dt->jiffies_frist_record);
        inputevent_data_save();
    } else {
        printk(KERN_INFO "%s: no input event\n", __func__);
    }
    return count;
}

static ssize_t show_record_user_ops(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%lu\n", ate_dt->is_record);
}

static DEVICE_ATTR(record_user_ops, (S_IWUSR|S_IRUGO),
	show_record_user_ops, store_record_user_ops);

static ssize_t show_play_user_ops(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    ate_dt->is_record = 0;
    ate_dt->cur_step = 0;
    if(0 >= ate_dt->idf.filer_total) {
        set_input_device_filter_array();
    }
    if (ate_dt->ie_times > 0) {
        wake_lock_timeout(&ate_dt->wake_lock, (ate_dt->playback_delay_ms / 1000 + 1) * HZ);
        schedule_delayed_work(&ate_dt->playback_work,
            msecs_to_jiffies(ate_dt->playback_delay_ms));
    }
    return sprintf(buf, "%lu\n", ate_dt->ie_times);
}

static DEVICE_ATTR(play_user_ops, (S_IWUSR|S_IRUGO),
	show_play_user_ops, NULL);

static ssize_t show_parse_user_ops_file(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    inputevent_data_read();
    return sprintf(buf, "%lu\n", ate_dt->ie_times);
}

static DEVICE_ATTR(parse_user_ops_file, (S_IWUSR|S_IRUGO),
	show_parse_user_ops_file, NULL);

static ssize_t show_playback_delay_ms(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%lu\n", ate_dt->playback_delay_ms);
}

static ssize_t store_playback_delay_ms(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;
    rc = strict_strtoul(buf, 0, &ate_dt->playback_delay_ms);
    return count;
}

static DEVICE_ATTR(playback_delay_ms, (S_IWUSR|S_IRUGO),
	show_playback_delay_ms, store_playback_delay_ms);

static ssize_t show_playback_continuance_ms(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%lu\n", ate_dt->playback_continuance_ms);
}

static DEVICE_ATTR(playback_continuance_ms, (S_IWUSR|S_IRUGO),
	show_playback_continuance_ms, NULL);

static ssize_t show_file_save_user_ops(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", ate_dt->iedt_file_dir);
}

static ssize_t store_file_save_user_ops(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
    int ret;

    memset(ate_dt->iedt_file_dir, 0, sizeof(ate_dt->iedt_file_dir));
    ret = sprintf(ate_dt->iedt_file_dir, "%s", buf);
    if (ret <= sizeof(ate_dt->iedt_file_dir) &&
        ate_dt->iedt_file_dir[ret - 1]=='\n') {
        ate_dt->iedt_file_dir[ret - 1] = '\0';
    }
    return ret;
}

static DEVICE_ATTR(file_save_user_ops, (S_IWUSR|S_IRUGO),
	show_file_save_user_ops, store_file_save_user_ops);

static int realbutton_to_virtualbutton(void)
{
    int i = 0;
    struct input_event_data *ie_data = NULL;
    struct input_dev *dev = NULL;

    ie_data = ate_dt->ie_dt + ate_dt->cur_step;
    for(i=0; i < ate_dt->ate_dev.input_dev_sum; i++) {
        if(TOUCH_EVENT == ate_dt->ate_dev.input_event_type[i]) {
            dev = ate_dt->ate_dev.input_dev_table[i];
            break ;
        }
    }
    if(i >= ate_dt->ate_dev.input_dev_sum) {
        printk(KERN_ERR"%s,get virturalbutton error!\n",__func__);
        return -1;
    }

#ifdef ATE_DEBUG
    printk("ate_dt->cur_step=%lu\n",ate_dt->cur_step);
#endif

    for(i = 0; i < ate_dt->virtualkey.total_dev; i++) {
#ifdef ATE_DEBUG
        printk("i=%d,ie_data->code=%d,ate_dt->virtualkey.virtualkey_device[i].code = %d\n",
		i,ie_data->code,ate_dt->virtualkey.virtualkey_device[i].code);
#endif
        if(ie_data->code== ate_dt->virtualkey.virtualkey_device[i].code) {
            break;
        }
    }
    if( i>= ate_dt->virtualkey.total_dev) {
        printk(KERN_ERR"%s,get virturalbutton error!\n",__func__);
        return -1;
    }

    if(TOUCH_BUTTON_EVENT == ie_data->dev_no && EV_KEY == ie_data->type ) {
        if(0 != ie_data->value) {
            input_event(dev, EV_ABS, ABS_MT_TOUCH_MAJOR, 17);
            input_event(dev, EV_ABS, ABS_MT_WIDTH_MAJOR, 2);
            input_event(dev, EV_ABS, ABS_MT_POSITION_X,
                ate_dt->virtualkey.virtualkey_device[i].l.x);
            input_event(dev, EV_ABS, ABS_MT_POSITION_Y,
                ate_dt->virtualkey.virtualkey_device[i].l.y);
            input_event(dev, EV_SYN, SYN_MT_REPORT, 0);
            input_event(dev, EV_SYN, SYN_REPORT, 0);
                ate_dt->cur_step ++;
        } else {
            input_event(dev, EV_SYN, SYN_MT_REPORT, 0);
            input_event(dev, EV_SYN, SYN_REPORT, 0);
            ate_dt->cur_step ++;
        }
    }
    return 0;
}


static void playback_work_handler(struct work_struct *work)
{
    struct input_event_data *ie_data = NULL;
    struct input_dev *dev = NULL;
    int value_temp = 0;
    int i;

    if (0 == ate_dt->cur_step) {
        ate_dt->jiffies_frist_playback = jiffies;
    }
    ie_data = ate_dt->ie_dt + ate_dt->cur_step;
    for(i=0; i < ate_dt->ate_dev.input_dev_sum; i++) {
        if(ie_data->dev_no == ate_dt->ate_dev.input_event_type[i]) {
	    dev = ate_dt->ate_dev.input_dev_table[i];
	    value_temp = ie_data->value;
#ifdef COHERENCE_FLATFORM
		if(TOUCH_EVENT == ie_data->dev_no && EV_ABS == ie_data->type ) {
			if(ABS_MT_POSITION_X== ie_data->code) {
			printk("+++++++++++++++++++++\n");
			value_temp = ie_data->value*dev->absinfo[ABS_MT_POSITION_X].maximum/MAX_VIRTUAL_SCREEN_SIZE;
			} else if (ABS_MT_POSITION_Y== ie_data->code ) {
			value_temp = ie_data->value*dev->absinfo[ABS_MT_POSITION_Y].maximum/MAX_VIRTUAL_SCREEN_SIZE;
			}
			printk("+++++++++value_temp = %d++++++++++++\n",value_temp);
		}
#endif
		input_event(dev, (unsigned int)ie_data->type, ie_data->code, value_temp);
            break ;
        }
    }
#ifdef COHERENCE_FLATFORM
    if(i >= ate_dt->ate_dev.input_dev_sum && true == ate_dt->has_virtualkey) {
        realbutton_to_virtualbutton();
    }
#endif
    if (ate_dt->cur_step < ate_dt->ie_times - 1) {
        if (jiffies >= ate_dt->jiffies_frist_playback + (ate_dt->ie_dt + (ate_dt->cur_step + 1))->jiffies_t) {
#ifdef ATE_DEBUG
            printk(KERN_DEBUG "%s: cur_step=%lu case 0 \n", __func__, ate_dt->cur_step);
#endif
            schedule_delayed_work(&ate_dt->playback_work, 0);
        } else {
            if(ate_dt->ie_times > 4 &&  ate_dt->cur_step >= ate_dt->ie_times - 4 &&
                POWER_KEY_EVENT == (ie_data + 1)->dev_no) {
#ifdef ATE_DEBUG
            printk(KERN_DEBUG "%s: cur_step=%lu case 1 \n", __func__, ate_dt->cur_step);
#endif
                schedule_delayed_work(&ate_dt->playback_work, 0);
            } else {
#ifdef ATE_DEBUG
            printk(KERN_DEBUG "%s: cur_step=%lu case 2 \n", __func__, ate_dt->cur_step);
#endif
                schedule_delayed_work(&ate_dt->playback_work,
                    ate_dt->jiffies_frist_playback + (ate_dt->ie_dt + (ate_dt->cur_step + 1))->jiffies_t - jiffies);
            }
        }
        ate_dt->cur_step++;
    }
}

static int readline(struct file *fp, char *buffer, loff_t *pos)
{
    int result = -1;
    int bytes_read = 0;
    /*------------------------------------------------------------------------*/
    /* Make sure the file descriptor is good */

    /* Read in from the file byte by byte until a newline is reached */
    while ((result = vfs_read(fp, &buffer[bytes_read], 1, pos)) == 1) {
        if (buffer[bytes_read] == '\r') {
            buffer[bytes_read] = '\0';
            bytes_read++;
        } else if (buffer[bytes_read] == '\n') {
            buffer[bytes_read] = '\0';
            bytes_read++;
            break;
        }
        bytes_read++;
    }
	/* Return the number of bytes read */
    if (result == -1)
        return result;
    else
        return bytes_read;
} /* readline */

static void clean_input_device_filter_array(void)
{
    int i = 0;

    for (i = 0; i < MAX_INPUT_DEV; i++) {
        memset(ate_dt->idf.dev_name[i], 0, MAX_SIZE_OF_DEV_NAME);
        ate_dt->ate_dev.valid[i] = false;
        ate_dt->ate_dev.input_event_type[i] = NULL_EVENT_TYPE;
        ate_dt->idf.input_event_type[i] = NULL_EVENT_TYPE;
    }
    ate_dt->idf.filer_total = 0;
}

static void default_set_input_device_filter_array(void )
{
#ifdef MSM_8960_PLATFORM
    strcpy(ate_dt->idf.dev_name[0], "atmel-touchscreen");
    ate_dt->idf.input_event_type[0] = TOUCH_EVENT;
    strcpy(ate_dt->idf.dev_name[1], "qt1060");
    ate_dt->idf.input_event_type[1] = TOUCH_BUTTON_EVENT;
    strcpy(ate_dt->idf.dev_name[2], "pmic8xxx_pwrkey");
    ate_dt->idf.input_event_type[2] = POWER_KEY_EVENT;
    strcpy(ate_dt->idf.dev_name[3], "keypad_8960");
    ate_dt->idf.input_event_type[3] = SOUND_KEY_EVENT;
    ate_dt->idf.filer_total = SOUND_KEY_EVENT+1;
#endif
#ifdef TI_OMAP_PLATFORM
    strcpy(ate_dt->idf.dev_name[0], "atmel-touchscreen");
    ate_dt->idf.input_event_type[0] = TOUCH_EVENT;
    strcpy(ate_dt->idf.dev_name[1], "qt1060");
    ate_dt->idf.input_event_type[1] = TOUCH_BUTTON_EVENT;
    strcpy(ate_dt->idf.dev_name[2], "twl6030_pwrbutton");
    ate_dt->idf.input_event_type[2] = POWER_KEY_EVENT;
    strcpy(ate_dt->idf.dev_name[3], "huawei_keypad");
    ate_dt->idf.input_event_type[3] = SOUND_KEY_EVENT;
    ate_dt->idf.filer_total = SOUND_KEY_EVENT+1;
#endif
#ifdef HISI_K3_PLATFORM
    strcpy(ate_dt->idf.dev_name[0], "synaptics");
    ate_dt->idf.input_event_type[0] = TOUCH_EVENT;
    strcpy(ate_dt->idf.dev_name[1], "qt1060");
    ate_dt->idf.input_event_type[1] = TOUCH_BUTTON_EVENT;
    strcpy(ate_dt->idf.dev_name[2], "k3v2_power_key");
    ate_dt->idf.input_event_type[2] = POWER_KEY_EVENT;
    strcpy(ate_dt->idf.dev_name[3], "k3v2_gpio_key");
    ate_dt->idf.input_event_type[3] = SOUND_KEY_EVENT;
    ate_dt->idf.filer_total = SOUND_KEY_EVENT+1;
#endif
}

static void set_input_device_filter_array(void )
{
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;
    int read_num = 0;
    int i = 0;
    int j = 0;

    fp = filp_open(HWATE_CONF_FILE, O_RDONLY , 0644);
    if (IS_ERR(fp)) {
        printk(KERN_ERR "%s: open file %s failed\n", __func__, HWATE_CONF_FILE);
        default_set_input_device_filter_array();
        printk(KERN_WARNING "%s: set default value.\n", __func__);
        goto set_input_device_arrary;
    } else {
        fs = get_fs();
        set_fs(KERNEL_DS);
        pos = 0;
        /* Read out the input dev name */
        while (1) {
            read_num = readline(fp, ate_dt->idf.dev_name[ate_dt->idf.filer_total], &pos);
            if (read_num <= 0 || read_num > MAX_SIZE_OF_DEV_NAME ||
                ate_dt->idf.filer_total > MAX_INPUT_DEV) {
                printk(KERN_ERR "%s: read name from %s error\n", __func__, HWATE_CONF_FILE);
                break;
            } else {
                ate_dt->idf.filer_total++;
            }
        }
        /* Close the file */
        filp_close(fp, NULL);
        set_fs(fs);

        for(i = 0; i < ate_dt->idf.filer_total; i++) {
            for(j = 0; j< sizeof(ate_dt->idf.dev_name[i]) - 1; j++) {
                if(' ' == ate_dt->idf.dev_name[i][j]) {
                    ate_dt->idf.dev_name[i][j] = '\0';
                    ate_dt->idf.input_event_type[i] = ( unsigned int ) (ate_dt->idf.dev_name[i][j+1] - '0');
                    if(ate_dt->idf.input_event_type[i] >= MAX_EVENT_TYPE) {
                        printk(KERN_ERR "%s: read error data from %s error\n", __func__, HWATE_CONF_FILE);
                        clean_input_device_filter_array();
                        return ;
                    }
                    break ;
                }
            }
        }

set_input_device_arrary:
        for (i = 0; i < ate_dt->idf.filer_total; i++) {
            for (j = 0; j < ate_dt->ate_dev.input_dev_sum; j++) {
                if (0 == strcmp(ate_dt->idf.dev_name[i], ate_dt->ate_dev.input_dev_table[j]->name)) {
#ifdef ATE_DEBUG
                    printk(KERN_DEBUG "%s: name=%s\n", __func__, ate_dt->idf.dev_name[i]);
                    printk(KERN_DEBUG "%s: i=%d j=%d\n", __func__, i, j);
#endif
                    ate_dt->ate_dev.valid[j] = true;
                    ate_dt->ate_dev.input_event_type[j] = ate_dt->idf.input_event_type[i];
                }
            }
        }
    }

}

static ssize_t store_input_device_filter(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long flag = 0;
    int ret = 0;

    ret = strict_strtoul(buf, 0, &flag);
    flag = !!flag;
    clean_input_device_filter_array();

    if (flag != 0) {
        set_input_device_filter_array();
    }
    return count;
}

static ssize_t show_input_device_filter(struct device *dev,
	struct device_attribute *attr, char *buf)
{
    int i = 0;
    char *s = buf;
    char *end = buf + PAGE_SIZE;

    for (i = 0; i < ate_dt->idf.filer_total; i++) {
        s += scnprintf(s, end - s, "%s\n", ate_dt->idf.dev_name[i]);
    }
    s += scnprintf(s, end - s, "\n");
    return (s - buf);
}

static DEVICE_ATTR(input_device_filter, (S_IWUSR | S_IRUGO),
	show_input_device_filter, store_input_device_filter);

#ifdef MSM_8960_PLATFORM
static void ate_usb_notifier(uint8_t usb_status)
{
	printk("+++++++%s+++++\n",__func__);
	ate_dt->usb_status= usb_status;
}
#endif

#ifdef HISI_K3_PLATFORM
static int ate_usb_notifier(struct notifier_block *nb,unsigned long event, void *data)
{
	printk("+++%s++event=%ld+++\n",__func__,event);
	ate_dt->usb_status= event;
	return NOTIFY_OK;
}
extern uint8_t usb_status_ate;
#endif

static ssize_t get_virtualkey_config(void)
{
    char c_buf[500];
    int i=0;
    char *p_char = NULL;
    int ret = 0;
    unsigned long value = 0;

#ifdef HISI_K3_PLATFORM
    ssize_t  buf_vkey_size=0;
    buf_vkey_size = sprintf(c_buf,
        __stringify(EV_KEY) ":" __stringify(KEY_BACK)   ":125:1380:160:90"
        ":" __stringify(EV_KEY) ":" __stringify(KEY_HOMEPAGE)   ":360:1380:160:90"
        ":" __stringify(EV_KEY) ":" __stringify(KEY_MENU) ":598:1380:160:90"
        "\n");
#endif

    if(buf_vkey_size <= 0) {
    return -1;
    }

#ifndef HISI_K3_PLATFORM
    memcpy(c_buf,buf_virtualkey,buf_vkey_size);
#endif
    for(i = ate_dt->virtualkey.total_dev = 0; i<500 &&
		i < buf_vkey_size; i++, ate_dt->virtualkey.total_dev++) {
        p_char = &c_buf[i];
        for(; i<500 && i < buf_vkey_size; i++) {
            if(':' == c_buf[i]) {
            c_buf[i] = '\0';
            break;
            }
        }

    if(!strcmp(p_char, __stringify(EV_KEY))) {
        ate_dt->virtualkey.virtualkey_device[ate_dt->virtualkey.total_dev].type = EV_KEY;
    } else {
    printk(KERN_ERR"the tepy of key is error\n");
    return -1;
    }

    p_char = &c_buf[++i];
    for(; i<500 && i < buf_vkey_size; i++) {
        if(':' == c_buf[i]) {
        c_buf[i] = '\0';
        break;
        }
    }

    ret = strict_strtoul(p_char, 0, &value);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
        return -1;
    }
    ate_dt->virtualkey.virtualkey_device[ate_dt->virtualkey.total_dev].code = (int)value;

    p_char = &c_buf[++i];
    for(; i<500 && i <= buf_vkey_size; i++) {
        if(':' == c_buf[i]) {
        c_buf[i] = '\0';
        break;
        }
    }

    ret = strict_strtoul(p_char, 0, &value);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
        return -1;
    }
    ate_dt->virtualkey.virtualkey_device[ate_dt->virtualkey.total_dev].l.x = (int)value;

    p_char = &c_buf[++i];
    for(; i<500 && i < buf_vkey_size; i++) {
        if(':' == c_buf[i]) {
        c_buf[i] = '\0';
        break;
        }
    }

    ret = strict_strtoul(p_char, 0, &value);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
        return -1;
    }
    ate_dt->virtualkey.virtualkey_device[ate_dt->virtualkey.total_dev].l.y = (int)value;

    p_char = &c_buf[++i];
    for(; i<500 && i <= buf_vkey_size; i++) {
        if(':' == c_buf[i]) {
        c_buf[i] = '\0';
        break;
        }
    }

    ret = strict_strtoul(p_char, 0, &value);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
        return -1;
    }
    ate_dt->virtualkey.virtualkey_device[ate_dt->virtualkey.total_dev].l.w = (int)value;

    p_char = &c_buf[++i];
    for(; i<500 && i < buf_vkey_size; i++) {
            if(':' == c_buf[i]) {
                c_buf[i] = '\0';
                break;
            } else if('\n' == c_buf[i]) {
                c_buf[i] = '\0';
                break;
            }
    }

    ret = strict_strtoul(p_char, 0, &value);
    if (ret < 0) {
        printk(KERN_ERR "%s: strict_strtoul ret error\n", __func__);
        return -1;
    }
    ate_dt->virtualkey.virtualkey_device[ate_dt->virtualkey.total_dev].l.h = (int)value;
    }
return 0;
}

static int __init ate_init(void)
{
    int ret;

    ate_dt = kmalloc(sizeof(struct ate_data), GFP_KERNEL);
    if(ate_dt == NULL) {
        ret = -ENOMEM;
        goto exit;
    }
    memset(ate_dt, 0, sizeof(struct ate_data));

    strcpy(ate_dt->iedt_file_dir, INPUT_EVENTS_DATA_FILE_DEFAULT);
    INIT_DELAYED_WORK_DEFERRABLE(&ate_dt->playback_work,
        playback_work_handler);
    wake_lock_init(&ate_dt->wake_lock, WAKE_LOCK_SUSPEND, "ate-wakelock");

#ifdef MSM_8960_PLATFORM
    notify_usb_status_to_ate(&ate_usb_notifier);
#endif

#ifdef HISI_K3_PLATFORM
    ate_dt->nb.notifier_call = ate_usb_notifier;
    ret = hiusb_charger_registe_notifier(&ate_dt->nb);
    if (ret < 0) {
        pr_err("k3ts, %s: failed to register hiusb_charger_registe_notifier\n", __func__);
    }
    ate_dt->usb_status = usb_status_ate;
#endif

    ate_dt->ie_dt = kmalloc(ATE_IEDT_SIZE, GFP_KERNEL);
    if (ate_dt->ie_dt == NULL) {
        ret = -ENOMEM;
        goto exit_kfree_ate_dt;
    }
    memset(ate_dt->ie_dt, 0, ATE_IEDT_SIZE);

#ifdef COHERENCE_FLATFORM
    if(0 != get_virtualkey_config()) {
        ate_dt->has_virtualkey = false;
    } else {
        ate_dt->has_virtualkey = true;
    }
#else
        ate_dt->has_virtualkey = false;
#endif

    hw_ate_kobj = kobject_create_and_add("hw_ate", NULL);
    if (hw_ate_kobj == NULL) {
        printk(KERN_ERR "%s: create hw_ate kobject failed\n", __func__);
        ret = -ENOMEM;
        goto exit_kfree_ie_dt;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_inputevents_count.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file inputevents_count failed\n", __func__);
        goto exit_kobject_del;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_record_user_ops.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file record_user_ops failed\n", __func__);
        goto exit_sysfs_remove_inputevents_count;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_play_user_ops.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file play_user_ops failed\n", __func__);
        goto exit_sysfs_remove_record_user_ops;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_parse_user_ops_file.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file parse_user_ops_file failed\n", __func__);
        goto exit_sysfs_remove_play_user_ops;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_playback_delay_ms.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file playback_delay_ms failed\n", __func__);
        goto exit_sysfs_remove_parse_user_ops;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_playback_continuance_ms.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file playback_continuance_ms failed\n", __func__);
        goto exit_sysfs_remove_playback_delay_ms;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_file_save_user_ops.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file file_save_user_ops failed\n", __func__);
        goto exit_sysfs_remove_playback_continuance_ms;
    }
    ret = sysfs_create_file(hw_ate_kobj, &dev_attr_input_device_filter.attr);
    if (ret) {
        printk(KERN_ERR "%s: sysfs_create_file input_event_filter failed\n", __func__);
        goto exit_sysfs_remove_file_save_user_ops;
    }
    return input_register_handler(&ate_handler);

exit_sysfs_remove_file_save_user_ops:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_file_save_user_ops.attr);
exit_sysfs_remove_playback_continuance_ms:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_playback_continuance_ms.attr);
exit_sysfs_remove_playback_delay_ms:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_playback_delay_ms.attr);
exit_sysfs_remove_parse_user_ops:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_parse_user_ops_file.attr);
exit_sysfs_remove_play_user_ops:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_play_user_ops.attr);
exit_sysfs_remove_record_user_ops:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_record_user_ops.attr);
exit_sysfs_remove_inputevents_count:
    sysfs_remove_file(hw_ate_kobj, &dev_attr_inputevents_count.attr);
exit_kobject_del:
    kobject_del(hw_ate_kobj);
exit_kfree_ie_dt:
    kfree(ate_dt->ie_dt);
exit_kfree_ate_dt:
    kfree(ate_dt);
exit:
    return ret;
}

static void __exit ate_exit(void)
{
    sysfs_remove_file(hw_ate_kobj, &dev_attr_input_device_filter.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_file_save_user_ops.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_playback_continuance_ms.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_playback_delay_ms.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_parse_user_ops_file.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_play_user_ops.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_record_user_ops.attr);
    sysfs_remove_file(hw_ate_kobj, &dev_attr_inputevents_count.attr);
    kobject_del(hw_ate_kobj);
    wake_unlock(&ate_dt->wake_lock);
    wake_lock_destroy(&ate_dt->wake_lock);
    kfree(ate_dt->ie_dt);
    kfree(ate_dt);

    input_unregister_handler(&ate_handler);
}

MODULE_AUTHOR("sunhonghui <sunhonghui@huawei.com>");
MODULE_DESCRIPTION("Huawei Automatic Test Engine Module");
MODULE_LICENSE("GPL");

module_init(ate_init);
module_exit(ate_exit);
