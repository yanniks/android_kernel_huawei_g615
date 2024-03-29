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

#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/thermal_framework.h>
#include <linux/temperature_sensor.h>
#include <linux/pm_qos_params.h>
#include <linux/ipps.h>
#include <hsad/config_mgr.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#endif
static struct thermal_dev *therm_fw;
static struct k3v2_die_governor *k3v2_gov;
static struct thermal_dev *ap_sensor;
static struct thermal_dev *sim_sensor;
static struct thermal_dev *k3v2_ondie_sensor;
static struct pm_qos_request_list g_cpumaxlimits;
static struct pm_qos_request_list g_gpumaxlimits;

#define COMFORT_BENCHMARK_THRESHOLD_HOT 79
#define COMFORT_BENCHMARK_THRESHOLD_COLD 70
#define NORMAL_TEMP_MONITORING_RATE 1000
#define COMFORT_THRESHOLD_HOT 58
#define COMFORT_THRESHOLD_COLD 55
#define PM_QOS_CPU_MAXPROFILE_VALUE 2000000
#define PM_QOS_CPU_PROFILE_LIMIT_VALUE 624000
#define PM_QOS_CPU_PROFILE_LIMIT_1_VALUE 416000
#define PM_QOS_GPU_MAXPROFILE_VALUE 1000000
#define PM_QOS_GPU_PROFILE_LIMIT_VALUE 120000
#define READ_TEMPERATURE_TIME  20  /*20 second*/
#define READ_TEMPERATURE_HOT_TIME  300 /*5 minute*/
#define ROOM_TEMPERATURE   25
struct k3v2_die_governor {
	struct thermal_dev *temp_sensor;
	int ap_temp;
	int sim_temp;
	int k3v2_temp;
	int governor_current_temp;
	int average_period;
	struct delayed_work average_cpu_sensor_work;
	int hot_temp_num;
	int cold_temp_num;
	int comfort_threshold_hot;
	int comfort_threshold_cold;
	bool k3v2_freq_is_lock;
	int exceed_temperature_time;
	struct work_struct regulator_otmp_wk;
	struct workqueue_struct *regulator_otmp_wq;
};
static int g_temp_bypass;
#ifdef CONFIG_IPPS_SUPPORT
static struct ipps_client ipps_client;
#endif
static int thermal_sensor_type = 0;
static enum sensor_type{
        APSENSOR,
        SIMSENSOR,
        UNKONWN
    };
#ifdef CONFIG_CPU_FREQ_GOV_K3HOTPLUG
/* pm_qos interface global val*/
struct pm_qos_lst {
	struct pm_qos_request_list *lst;
	int qos_class;
	s32 dvalue;
};
static struct pm_qos_lst pm_qos_temperature_list[] = {
{&g_cpumaxlimits, PM_QOS_CPU_MAX_PROFILE, PM_QOS_CPU_MAXPROFILE_DEFAULT_VALUE},
{&g_gpumaxlimits, PM_QOS_GPU_MAX_PROFILE, PM_QOS_GPU_MAXPROFILE_DEFAULT_VALUE},
};

/******************** CPU GPU FOR TEMP LIMIT***************************/
void k3v2_temperature_pm_qos_add(void)
{
	int i = 0;
	int ilength = sizeof(pm_qos_temperature_list)/sizeof(struct pm_qos_lst);

	for (i = 0; i < ilength; i++) {
		pm_qos_add_request(pm_qos_temperature_list[i].lst, pm_qos_temperature_list[i].qos_class,
			pm_qos_temperature_list[i].dvalue);
	}
}

void k3v2_temperature_pm_qos_remove(void)
{
	int i = 0;
	int ilength = sizeof(pm_qos_temperature_list)/sizeof(struct pm_qos_lst);

	for (i = 0; i < ilength; i++)
		pm_qos_remove_request(pm_qos_temperature_list[i].lst);
}
#else
void k3v2_temperature_pm_qos_add(void)
{
}
void k3v2_temperature_pm_qos_remove(void)
{
}
#endif
static int k3v2_cpu_thermal_manager(int temp)
{
	if (temp > k3v2_gov->comfort_threshold_hot) {
		k3v2_gov->hot_temp_num++;
		k3v2_gov->cold_temp_num = 0;
	} else if (temp < k3v2_gov->comfort_threshold_cold) {
		k3v2_gov->cold_temp_num++;
		k3v2_gov->hot_temp_num = 0;
	} else {
		k3v2_gov->hot_temp_num = 0;
		k3v2_gov->cold_temp_num = 0;
	}
#ifdef CONFIG_CPU_FREQ_GOV_K3HOTPLUG
	if ((temp > k3v2_gov->comfort_threshold_hot) && (READ_TEMPERATURE_HOT_TIME <= k3v2_gov->hot_temp_num)) {

		k3v2_gov->exceed_temperature_time++;
		if (3 <= k3v2_gov->exceed_temperature_time) {
			/*temperature exceed 58 over 15 minutes */
			pr_warning("Device temperature is very high,board temperature is [%d]!\n\r", temp);
		} else if (2 == k3v2_gov->exceed_temperature_time) {
			/*temperature exceed 58 over 10 minutes*/
			pr_info("thermal_manager2:temperature_time[%d]\n\r", k3v2_gov->exceed_temperature_time);
			pm_qos_update_request(&g_cpumaxlimits, PM_QOS_CPU_PROFILE_LIMIT_1_VALUE);
		} else {
			pr_info("thermal_manager:temperature_time[%d]\n\r", k3v2_gov->exceed_temperature_time);
		}

		if (k3v2_gov->k3v2_freq_is_lock == false) {
			/*temperature exceed 58 over 5 minutes*/
			pm_qos_update_request(&g_cpumaxlimits, PM_QOS_CPU_PROFILE_LIMIT_VALUE);
			pm_qos_update_request(&g_gpumaxlimits, PM_QOS_GPU_PROFILE_LIMIT_VALUE);
			k3v2_gov->hot_temp_num = 0;
			k3v2_gov->cold_temp_num = 0;
			k3v2_gov->k3v2_freq_is_lock = true;
			pr_info("COMFORT_THRESHOLD_HOT limit \n\r");
		}
	} else if ((temp < k3v2_gov->comfort_threshold_cold) && (READ_TEMPERATURE_TIME <= k3v2_gov->cold_temp_num)) {
		k3v2_gov->exceed_temperature_time = 0;
		if (k3v2_gov->k3v2_freq_is_lock == true) {
			pm_qos_update_request(&g_cpumaxlimits, PM_QOS_CPU_MAXPROFILE_DEFAULT_VALUE);
			pm_qos_update_request(&g_gpumaxlimits, PM_QOS_GPU_MAXPROFILE_DEFAULT_VALUE);
			k3v2_gov->hot_temp_num = 0;
			k3v2_gov->cold_temp_num = 0;
			k3v2_gov->k3v2_freq_is_lock = false;
			pr_info("COMFORT_THRESHOLD_COLD release \n\r");
		}
	}
#endif
	if (k3v2_gov->hot_temp_num >= READ_TEMPERATURE_HOT_TIME) {
		k3v2_gov->hot_temp_num = 0;
	} else if (k3v2_gov->cold_temp_num >= READ_TEMPERATURE_TIME) {
		k3v2_gov->cold_temp_num = 0;
	}

	return 0;
}

#ifdef CONFIG_CPU_FREQ_GOV_K3HOTPLUG
static int k3v2_benchmark_thermal_manager(int temp)
{
	if (temp > COMFORT_BENCHMARK_THRESHOLD_HOT) {
		pm_qos_update_request(&g_cpumaxlimits, PM_QOS_CPU_PROFILE_LIMIT_1_VALUE);
		pm_qos_update_request(&g_gpumaxlimits, PM_QOS_GPU_PROFILE_LIMIT_VALUE);
	} else if(temp < COMFORT_BENCHMARK_THRESHOLD_COLD) {
		pm_qos_update_request(&g_cpumaxlimits, PM_QOS_CPU_MAXPROFILE_VALUE);
		pm_qos_update_request(&g_gpumaxlimits, PM_QOS_GPU_MAXPROFILE_VALUE);
	}
}
#endif

static int average_on_die_temperature(void)
{
	int temp = 0;

#if defined(CONFIG_K3V2_CPU_TEMP_SENSOR) && defined(CONFIG_K3V2_AP_SENSOR) && defined(CONFIG_K3V2_SIM_SENSOR)
	if (ap_sensor && sim_sensor && k3v2_ondie_sensor) {
		k3v2_gov->ap_temp = thermal_request_temp(ap_sensor);
		k3v2_gov->sim_temp = thermal_request_temp(sim_sensor);
		k3v2_gov->k3v2_temp = thermal_request_temp(k3v2_ondie_sensor);
		pr_info("[brand] %s,ap_temp[%d],sim_temp[%d],k3v2[%d]\n\r", __func__,
			k3v2_gov->ap_temp, k3v2_gov->sim_temp, k3v2_gov->k3v2_temp);

		temp = (k3v2_gov->sim_temp + k3v2_gov->ap_temp + k3v2_gov->k3v2_temp) / 3;
	}
#elif defined(CONFIG_K3V2_AP_SENSOR) && defined(CONFIG_K3V2_SIM_SENSOR)
	if ((ap_sensor) && (APSENSOR == thermal_sensor_type )) {
		k3v2_gov->ap_temp = thermal_request_temp(ap_sensor);
		temp = k3v2_gov->ap_temp;/*judgement base on ap temp */
//		pr_info("[brand] %s,ap_temp[%d]\n\r", __func__,k3v2_gov->ap_temp);
	}
	else if ((sim_sensor) && (SIMSENSOR == thermal_sensor_type)) {
		k3v2_gov->sim_temp = thermal_request_temp(sim_sensor);
		temp = k3v2_gov->sim_temp;/*judgement base on SIM temp */
	}
#endif
	else {
		temp = -1;
		pr_info("%s: canot get ap,sim,k3v2 temp \n\r", __func__);
	}
	return temp;
}

static void average_cpu_sensor_delayed_work_fn(struct work_struct *work)
{
	struct k3v2_die_governor *k3v2_gov =
				container_of(work, struct k3v2_die_governor,
						average_cpu_sensor_work.work);
	unsigned int upolicy = 0;
	ipps_get_current_policy(&ipps_client, IPPS_OBJ_CPU, &upolicy);

	if (0 == g_temp_bypass) {
		k3v2_gov->governor_current_temp = average_on_die_temperature();
	} else {
		k3v2_gov->governor_current_temp = ROOM_TEMPERATURE;
	}

	if (k3v2_gov->governor_current_temp > 0) {
		if (PM_QOS_IPPS_POLICY_SPECIAL0B == ((upolicy >> 4) & 0x0F)) {
			k3v2_benchmark_thermal_manager(k3v2_gov->governor_current_temp);
		} else {
			k3v2_cpu_thermal_manager(k3v2_gov->governor_current_temp);
		}
	}

	schedule_delayed_work(&k3v2_gov->average_cpu_sensor_work,
				msecs_to_jiffies(k3v2_gov->average_period));
}

static int k3v2_process_cpu_temp(struct thermal_dev *temp_sensor, int temp)
{
	k3v2_gov->temp_sensor = temp_sensor;

	if (!strcmp(temp_sensor->name, "ap_sensor")) {
		if (ap_sensor == NULL) {
			pr_info("%s: Setting %s pointer\n",
				__func__, temp_sensor->name);
			ap_sensor = temp_sensor;
		}
		k3v2_gov->ap_temp = temp;
	} else if (!strcmp(temp_sensor->name, "sim_sensor")) {
		if (sim_sensor == NULL) {
			pr_info("%s: Setting %s pointer\n",
				__func__, temp_sensor->name);
			sim_sensor = temp_sensor;
		}
		k3v2_gov->sim_temp = temp;
	} else if (!strcmp(temp_sensor->name, "k3v2_ondie_sensor")) {
		if (k3v2_ondie_sensor == NULL) {
			pr_info("%s: Setting %s pointer\n",
				__func__, temp_sensor->name);
			k3v2_ondie_sensor = temp_sensor;
		}
		k3v2_gov->k3v2_temp = temp;
	} else {
		pr_err("Get AP OR SIM OR K3V2 CPU temperature failed!\n\r");
		return -1;
	}
	return 0;
}
static int get_firmware_thermal_temp(void)
{
    char product_flag[15];
	bool ret =0;
	int temp = 0;
    if ( get_hw_config_int("thermal_temp/hot_temp", &temp, NULL))
		k3v2_gov->comfort_threshold_hot = temp;
	else
	    pr_info("[brand] %s,comfort_threshold_hot_get error\n\r", __func__);
	if (get_hw_config_int("thermal_temp/cold_temp", &temp, NULL))
		k3v2_gov->comfort_threshold_cold = temp;
	else
	    pr_info("[brand] %s,comfort_threshold_cold_get error\n\r", __func__);
	ret = get_hw_config_string("thermal_temp/sensor_type",product_flag,15,NULL);
	if(ret)
	{
		if(strstr(product_flag,"apsensor"))
		{
			printk("sensor is near ap\r\n");
			return APSENSOR;
		}else{
		    printk("sensor is near sim card\r\n");
			return SIMSENSOR;
		}
	}else{
	   printk("sensor_type is error!\r\n"); 
	   return UNKONWN;
	}		
}
static struct thermal_dev_ops k3v2_gov_ops = {
	.process_temp = k3v2_process_cpu_temp,
};

#ifdef CONFIG_DEBUG_FS
static int dbg_temp_governor_show(struct seq_file *s, void *unused)
{
	seq_printf(s, "temp[%d]hot_num[%d],cold_num[%d]lock_status[%d],exceed_time[%d],temp_bypass[%d]\n\r",
		k3v2_gov->governor_current_temp,
		k3v2_gov->hot_temp_num,
		k3v2_gov->cold_temp_num,
		k3v2_gov->k3v2_freq_is_lock,
		k3v2_gov->exceed_temperature_time,
		g_temp_bypass);

	return 0;
}
static ssize_t dbg_temp_governor_set_value(struct file *filp, const char __user *buffer,
	size_t count, loff_t *ppos)
{
	char tmp[128] = {0};
	int index = 0;

	if (count > 128) {
		pr_info("error! buffer size big than internal buffer\n");
		return -EFAULT;
	}

	if (copy_from_user(tmp, buffer, count)) {
		pr_info("error!\n");
		return -EFAULT;
	}

	if (sscanf(tmp, "%d", &index)) {
		if (index) {
			g_temp_bypass = 1;
		} else {
			g_temp_bypass = 0;
		}
	} else {
		pr_info("ERRR~\n");
	}

	*ppos += count;

	return count;
}

static int dbg_temp_governor_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return single_open(file, dbg_temp_governor_show, &inode->i_private);
}

static const struct file_operations debug_temp_governor_fops = {
	.open		= dbg_temp_governor_open,
	.read		= seq_read,
	.write		= dbg_temp_governor_set_value,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

#ifdef CONFIG_IPPS_SUPPORT

/******************k3v2_temp_governor driver interface begin*****************************/

static void ippsclient_add(struct ipps_device *device)
{
}

static void ippsclient_remove(struct ipps_device *device)
{
}

static struct ipps_client ipps_client = {
	.name   = "k3v2_temp_governor",
	.add    = ippsclient_add,
	.remove = ippsclient_remove
};

/*****************ipps driver interface end ****************************/

#endif

static inline void dbs_timer_exit(void)
{
	cancel_delayed_work_sync(&k3v2_gov->average_cpu_sensor_work);
}

static int k3v2_temperature_reboot_notify(struct notifier_block *nb,
				unsigned long code, void *unused)
{
	if ((code == SYS_RESTART) || (code == SYS_POWER_OFF) ||
		(code == SYS_HALT)) {
		printk("k3v2_temperature_reboot_notify code 0x%lx stop read ADC\n", code);
		dbs_timer_exit();
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block k3v2_temperature_reboot_nb = {
	.notifier_call	= k3v2_temperature_reboot_notify,
	.next		= NULL,
	.priority	= INT_MAX, /* before any real devices */
};

static void dispose_pmu_otmp(struct work_struct *work)
{
#ifdef CONFIG_CPU_FREQ_GOV_K3HOTPLUG
	pm_qos_update_request(&g_cpumaxlimits, PM_QOS_CPU_PROFILE_LIMIT_1_VALUE);
	pm_qos_update_request(&g_gpumaxlimits, PM_QOS_GPU_PROFILE_LIMIT_VALUE);
#endif
	k3v2_gov->hot_temp_num = 0;
	k3v2_gov->cold_temp_num = 0;
	k3v2_gov->k3v2_freq_is_lock = true;
}

static irqreturn_t pmu_temperature_over_support(int irq, void *data)
{
	queue_work(k3v2_gov->regulator_otmp_wq, &k3v2_gov->regulator_otmp_wk);
	return IRQ_HANDLED;
}

static int __init k3v2_die_governor_init(void)
{
	struct thermal_dev *thermal_fw;
	struct dentry *d;
	int ret = 0;

	k3v2_gov = kzalloc(sizeof(struct k3v2_die_governor), GFP_KERNEL);
	if (!k3v2_gov) {
		pr_err("%s:Cannot allocate memory\n", __func__);
		return -ENOMEM;
	}

	thermal_fw = kzalloc(sizeof(struct thermal_dev), GFP_KERNEL);
	if (NULL == thermal_fw) {
		pr_err("%s: Cannot allocate memory\n", __func__);
		ret =  -ENOMEM;
		goto therm_fw_get_err;
	}

	thermal_fw->name = "k3v2_ondie_governor";
	thermal_fw->domain_name = "cpu";
	thermal_fw->dev_ops = &k3v2_gov_ops;
	thermal_governor_dev_register(thermal_fw);
	therm_fw = thermal_fw;

	ap_sensor = NULL;
	sim_sensor = NULL;
	k3v2_ondie_sensor = NULL;
	k3v2_gov->hot_temp_num = 0;
	k3v2_gov->cold_temp_num = 0;
	k3v2_gov->exceed_temperature_time = 0;
	k3v2_gov->k3v2_freq_is_lock = false;
	g_temp_bypass = 0;
	k3v2_gov->comfort_threshold_hot = 0;
	k3v2_gov->comfort_threshold_cold = 0;
	thermal_sensor_type = get_firmware_thermal_temp();
	k3v2_temperature_pm_qos_add();
	/* Init delayed work to average on-die temperature */
	INIT_DELAYED_WORK(&k3v2_gov->average_cpu_sensor_work,
			  average_cpu_sensor_delayed_work_fn);
	k3v2_gov->average_period = NORMAL_TEMP_MONITORING_RATE;

	schedule_delayed_work(&k3v2_gov->average_cpu_sensor_work,
			msecs_to_jiffies(0));
#ifdef CONFIG_DEBUG_FS
	d = debugfs_create_dir("temp_governor", NULL);
	if (!d) {
		ret = -ENOMEM;
		goto debugfs_create_get_err;
	}
	(void) debugfs_create_file("k3v2_temp_governor", S_IRUSR,
					d, NULL, &debug_temp_governor_fops);
#endif
	register_reboot_notifier(&k3v2_temperature_reboot_nb);

	/*register regulator otmp interrupt*/
	k3v2_gov->regulator_otmp_wq = create_singlethread_workqueue("hi6421_regulator_otmp");
	INIT_WORK(&k3v2_gov->regulator_otmp_wk, (void *)dispose_pmu_otmp);

	ret = request_irq(IRQ_OTMP_RISING, pmu_temperature_over_support, IRQF_DISABLED, "hi6421-irq", k3v2_gov);
	if (ret) {
		printk("hi6421 irq register ocp interrupt failed!\n");
		goto debugfs_create_get_err;
	}

#ifdef CONFIG_IPPS_SUPPORT
	ret = ipps_register_client(&ipps_client);
	if (ret != 0) {
		pr_err("%s ipps_register_client err=%x\n",
			__func__, ret);
		goto debugfs_create_get_err;
	}
#endif

	return ret;

debugfs_create_get_err:
	kfree(therm_fw);
therm_fw_get_err:
	kfree(k3v2_gov);

	return ret;
}

static void __exit k3v2_die_governor_exit(void)
{
#ifdef CONFIG_IPPS_SUPPORT
	ipps_unregister_client(&ipps_client);
#endif
	cancel_delayed_work_sync(&k3v2_gov->average_cpu_sensor_work);
	k3v2_temperature_pm_qos_remove();
	thermal_governor_dev_unregister(therm_fw);
	kfree(therm_fw);
	kfree(k3v2_gov);
	unregister_reboot_notifier(&k3v2_temperature_reboot_nb);
}

module_init(k3v2_die_governor_init);
module_exit(k3v2_die_governor_exit);

MODULE_AUTHOR("Brand  <xuezhiliang@huawei.com>");
MODULE_DESCRIPTION("k3v2 on-die thermal governor");
MODULE_LICENSE("GPL");
