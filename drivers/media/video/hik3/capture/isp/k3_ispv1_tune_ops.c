/*
 *  Hisilicon K3 soc camera ISP driver source file
 *
 *  CopyRight (C) Hisilicon Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111 - 1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <asm/io.h>
#include <asm/bug.h>
#include <linux/poll.h>
#include <linux/interrupt.h>
#include <linux/android_pmem.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/time.h>

#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include <hsad/config_interface.h>
#include <mach/boardid.h>
#include "cam_util.h"
#include "cam_dbg.h"
#include "k3_isp.h"
#include "k3_ispv1.h"
#include "k3_ispv1_afae.h"
#include "k3_isp_io.h"

#define DEBUG_DEBUG 0
#define LOG_TAG "K3_ISPV1_TUNE_OPS"
#include "cam_log.h"

k3_isp_data *this_ispdata;
static bool camera_ajustments_flag;

static int scene_target_y_low = DEFAULT_TARGET_Y_LOW;
static int scene_target_y_high = DEFAULT_TARGET_Y_HIGH;
bool fps_lock;

static bool ispv1_change_frame_rate(
	camera_frame_rate_state *state, camera_frame_rate_dir direction, camera_sensor *sensor);
static int ispv1_set_frame_rate(camera_frame_rate_mode mode, camera_sensor *sensor);

/*
 * For anti-shaking, y36721 todo
 * ouput_width-2*blocksize
 * ouput_height-2*blocksize
*/
int ispv1_set_anti_shaking_block(int blocksize)
{
	camera_rect_s out, stat;
	u8 reg1;

	print_debug("enter %s", __func__);

	/* bit4: 0-before yuv downscale;1-after yuv dcw, but before yuv upscale */
	reg1 = GETREG8(REG_ISP_SCALE6_SELECT);
	reg1 |= (1 << 4);
	SETREG8(REG_ISP_SCALE6_SELECT, reg1);

	/* y36721 todo */
	out.left = 0;
	out.top = 0;

	out.width = blocksize;
	out.height = blocksize;

	k3_isp_antishaking_rect_out2stat(&out, &stat);

	SETREG8(REG_ISP_ANTI_SHAKING_BLOCK_SIZE, (stat.width & 0xff00) >> 8);
	SETREG8(REG_ISP_ANTI_SHAKING_BLOCK_SIZE + 1, stat.width & 0xff);

	/*
	 * y36721 todo
	 * should set REG_ISP_ANTI_SHAKING_ABSSTART_POSITION_H and V
	 * REG_ISP_ANTI_SHAKING_ABSSTART_POSITION_H
	 * REG_ISP_ANTI_SHAKING_ABSSTART_POSITION_V
	 */

	return 0;
}

int ispv1_set_anti_shaking(camera_anti_shaking flag)
{
	print_debug("enter %s", __func__);

	if (flag)
		SETREG8(REG_ISP_ANTI_SHAKING_ENABLE, 1);
	else
		SETREG8(REG_ISP_ANTI_SHAKING_ENABLE, 0);

	return 0;
}

/* read out anti-shaking coordinate */
int ispv1_get_anti_shaking_coordinate(coordinate_s *coordinate)
{
	u8 reg0;
	camera_rect_s out, stat;

	print_debug("enter %s", __func__);

	reg0 = GETREG8(REG_ISP_ANTI_SHAKING_ENABLE);

	if ((reg0 & 0x1) != 1) {
		print_error("anti_shaking not working!");
		return -1;
	}

	GETREG16(REG_ISP_ANTI_SHAKING_WIN_LEFT, stat.left);
	GETREG16(REG_ISP_ANTI_SHAKING_WIN_TOP, stat.top);
	stat.width = 0;
	stat.height = 0;

	k3_isp_antishaking_rect_stat2out(&out, &stat);

	coordinate->x = out.left;
	coordinate->y = out.top;

	return 0;

}

/* Added for ISO, target Y will not change with ISO */
int ispv1_set_iso(camera_iso iso)
{
	int max_iso, min_iso;
	int max_gain, min_gain;
	int retvalue = 0;
	camera_sensor *sensor;
	if (NULL == this_ispdata) {
		print_info("this_ispdata is NULL");
		return -1;
	}
	sensor = this_ispdata->sensor;

	print_debug("enter %s", __func__);

	/* ISO is to change sensor gain, but is not same */
	switch (iso) {
	case CAMERA_ISO_AUTO:
		/*
		 * max iso should be ISO1600
		 * min iso should be ISO100
		 */
		max_iso = 1550;	/*max is 1550 for ov8830 */
		min_iso = 100;
		break;

	case CAMERA_ISO_100:
		max_iso = (100 + (100 / 8) * 2);
		min_iso = 100;
		break;

	case CAMERA_ISO_200:
		/* max and min iso should be fixed ISO200 */
		max_iso = (200 + 200 / 8);
		min_iso = (200 - 200 / 8);
		break;

	case CAMERA_ISO_400:
		/* max and min iso should be fixed ISO400 */
		max_iso = (400 + 400 / 8);
		min_iso = (400 - 400 / 8);
		break;

	case CAMERA_ISO_800:
		/* max and min iso should be fixed ISO800 */
		max_iso = 775;
		min_iso = 650;
		break;

	default:
		retvalue = -1;
		goto out;
		break;
	}

	if (sensor->sensor_iso_to_gain) {
		max_gain = sensor->sensor_iso_to_gain(max_iso);
		min_gain = sensor->sensor_iso_to_gain(min_iso);
	} else {
		print_error("sensor_iso_to_gain not defined!");
		retvalue = -1;
		goto out;
	}

	if ((max_gain <= 0) || (min_gain <= 0)) {
		retvalue = -1;
		goto out;
	} else {
		/* set to ISP registers */
		SETREG8(REG_ISP_MAX_GAIN, (max_gain & 0xff00) >> 8);
		SETREG8(REG_ISP_MAX_GAIN + 1, max_gain & 0xff);
		SETREG8(REG_ISP_MIN_GAIN, (min_gain & 0xff00) >> 8);
		SETREG8(REG_ISP_MIN_GAIN + 1, min_gain & 0xff);

		sensor->min_gain = min_gain;
		sensor->max_gain = max_gain;
	}

out:
	return retvalue;
}

int inline ispv1_iso2gain(int iso, bool binning)
{
	int gain;

	if (binning == false)
		iso *= 2;

	gain = iso * 0x10 / 100;
	return gain;
}

int inline ispv1_gain2iso(int gain, bool binning)
{
	int iso;

	iso = gain * 100 / 0x10;
	if (binning == false)
		iso /= 2;

	iso = (iso + 5) / 10 * 10;
	return iso;
}

/*
 * only useful for ISO auto mode
 */
int ispv1_get_actual_iso(void)
{
	camera_sensor *sensor = this_ispdata->sensor;
	int gain = get_writeback_gain();
	int iso;
	int index;
	bool binning;

	gain = get_writeback_gain();

	if (isp_hw_data.cur_state == STATE_PREVIEW)
		index = sensor->preview_frmsize_index;
	else
		index = sensor->capture_frmsize_index;
	binning = sensor->frmsize_list[index].binning;

	iso = ispv1_gain2iso(gain, binning);
	return iso;
}

/* real exposure time is pWriteBackExpo[0x1c79c-0x1c79f] */
int ispv1_get_exposure_time(void)
{
	u32 expo_line, fps, vts, index;
	int denominator_expo_time;
	camera_sensor *sensor = this_ispdata->sensor;

	if (isp_hw_data.cur_state == STATE_PREVIEW)
		index = sensor->preview_frmsize_index;
	else
		index = sensor->capture_frmsize_index;

	expo_line = get_writeback_expo() >> 4;
	fps = sensor->frmsize_list[index].fps;
	vts = sensor->frmsize_list[index].vts;

	denominator_expo_time = ispv1_expo_line2time(expo_line, fps, vts);
	return denominator_expo_time;
}
u32 ispv1_get_awb_gain(int withShift)
{
	u16 b_gain, r_gain;
	u32 return_val;
	if (withShift) {
		GETREG16(REG_ISP_AWB_GAIN_B, b_gain);
		GETREG16(REG_ISP_AWB_GAIN_R, r_gain);
	} else {
		GETREG16(REG_ISP_AWB_ORI_GAIN_B, b_gain);
		GETREG16(REG_ISP_AWB_ORI_GAIN_R, r_gain);
	}
	return_val = (b_gain << 16) | r_gain;
	return return_val;
}

u32 ispv1_get_expo_line(void)
{
	u32 ret;

	ret = get_writeback_expo();
	return ret;
}

u32 ispv1_get_sensor_vts(void)
{
	camera_sensor *sensor;
	u32 frame_index;
	u32 full_fps, fps;
	u32 basic_vts;
	u32 vts;

	if (NULL == this_ispdata) {
		print_info("this_ispdata is NULL");
		return 0;
	}
	sensor = this_ispdata->sensor;

	if (isp_hw_data.cur_state == STATE_PREVIEW)
		frame_index = sensor->preview_frmsize_index;
	else
		frame_index = sensor->capture_frmsize_index;

	full_fps = sensor->frmsize_list[frame_index].fps;
	basic_vts = sensor->frmsize_list[frame_index].vts;
	fps = sensor->fps;
	vts = basic_vts * full_fps / fps;

	return vts;
}

u32 ispv1_get_current_ccm_rgain(void)
{
	return GETREG8(REG_ISP_CCM_PREGAIN_R);
}

u32 ispv1_get_current_ccm_bgain(void)
{
	return GETREG8(REG_ISP_CCM_PREGAIN_B);
}

/* Added for EV: exposure compensation.
 * Just set as this: ev+2 add 40%, ev+1 add 20% to current target Y
 * should config targetY and step
 */
void ispv1_calc_ev(u8 *target_low, u8 *target_high, int ev)
{
	int target_y_low = DEFAULT_TARGET_Y_LOW;
	int target_y_high = DEFAULT_TARGET_Y_HIGH;

	switch (ev) {
	case -2:
		target_y_low = DEFAULT_TARGET_Y_LOW - 0x10;
		target_y_low *= (EV_RATIO_NUMERATOR * EV_RATIO_NUMERATOR);
		target_y_low /= (EV_RATIO_DENOMINATOR * EV_RATIO_DENOMINATOR);
		target_y_high = target_y_low + 2;
		break;

	case -1:
		target_y_low = DEFAULT_TARGET_Y_LOW - 0x06;
		target_y_low *= EV_RATIO_NUMERATOR;
		target_y_low /= EV_RATIO_DENOMINATOR;
		target_y_high = target_y_low + 2;
		break;

	case 0:
		break;

	case 1:
		target_y_low = DEFAULT_TARGET_Y_LOW + 0x18;
		target_y_low *= EV_RATIO_DENOMINATOR;
		target_y_low /= EV_RATIO_NUMERATOR;
		target_y_high = target_y_low + 2;
		break;

	case 2:
		target_y_low = DEFAULT_TARGET_Y_LOW + 0x0e;
		target_y_low *= (EV_RATIO_DENOMINATOR * EV_RATIO_DENOMINATOR);
		target_y_low /= (EV_RATIO_NUMERATOR * EV_RATIO_NUMERATOR);
		target_y_high = target_y_low + 2;
		break;

	default:
		print_error("ev invalid");
		break;
	}

	*target_low = target_y_low;
	*target_high = target_y_high;
}

/* Added for EV: exposure compensation.
 * Just set as this: ev+2 add 40%, ev+1 add 20% to current target Y
 * should config targetY and step
 */
int ispv1_set_ev(int ev)
{
#ifdef OVISP_DEBUG_MODE
	return 0;
#endif
	u8 target_y_low, target_y_high;
	int ret = 0;

	print_debug("enter %s", __func__);

	/* ev is target exposure compensation value, decided by exposure time and sensor gain */
	ispv1_calc_ev(&target_y_low, &target_y_high, ev);

	if ((ev == 0) && (this_ispdata->ev != 0)) {
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_low);
		msleep(100);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
	} else {
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
	}

	scene_target_y_low = target_y_low;
	scene_target_y_high = target_y_high;

	return ret;
}

static void ispv1_init_sensor_config(camera_sensor *sensor)
{
	/* Enable AECAGC */
	SETREG8(REG_ISP_AECAGC_MANUAL_ENABLE, AUTO_AECAGC);
#if 1				/* new firmware support isp write all sensors's AEC/AGC registers. */
	SETREG8(REG_ISP_AECAGC_WRITESENSOR_ENABLE, ISP_WRITESENSOR_ENABLE);
#else
	/* y36721 2012-03-28 added temporarily */
	if (sensor->sensor_type == SENSOR_OV)
		SETREG8(REG_ISP_AECAGC_WRITESENSOR_ENABLE, ISP_WRITESENSOR_ENABLE);
	else
		SETREG8(REG_ISP_AECAGC_WRITESENSOR_ENABLE, ISP_WRITESENSOR_DISABLE);
#endif

	if (sensor->sensor_type == SENSOR_OV)
		SETREG8(REG_ISP_TOP6, SENSOR_BGGR);
	else if (sensor->sensor_type == SENSOR_SONY)
		SETREG8(REG_ISP_TOP6, SENSOR_RGGB);
	else if (sensor->sensor_type == SENSOR_SAMSUNG)
		SETREG8(REG_ISP_TOP6, SENSOR_GRBG);
	else
		SETREG8(REG_ISP_TOP6, SENSOR_BGGR);	/* default set same as OV */

	SETREG16(REG_ISP_AEC_ADDR0, sensor->aec_addr[0]);
	SETREG16(REG_ISP_AEC_ADDR1, sensor->aec_addr[1]);
	SETREG16(REG_ISP_AEC_ADDR2, sensor->aec_addr[2]);

	SETREG16(REG_ISP_AGC_ADDR0, sensor->agc_addr[0]);
	SETREG16(REG_ISP_AGC_ADDR1, sensor->agc_addr[1]);

	if (0 == sensor->aec_addr[0])
		SETREG8(REG_ISP_AEC_MASK_0, 0x00);
	else
		SETREG8(REG_ISP_AEC_MASK_0, 0xff);

	if (0 == sensor->aec_addr[1])
		SETREG8(REG_ISP_AEC_MASK_1, 0x00);
	else
		SETREG8(REG_ISP_AEC_MASK_1, 0xff);

	if (0 == sensor->aec_addr[2])
		SETREG8(REG_ISP_AEC_MASK_2, 0x00);
	else
		SETREG8(REG_ISP_AEC_MASK_2, 0xff);

	if (0 == sensor->agc_addr[0])
		SETREG8(REG_ISP_AGC_MASK_H, 0x00);
	else
		SETREG8(REG_ISP_AGC_MASK_H, 0xff);

	if (0 == sensor->agc_addr[1])
		SETREG8(REG_ISP_AGC_MASK_L, 0x00);
	else
		SETREG8(REG_ISP_AGC_MASK_L, 0xff);

	SETREG8(REG_ISP_AGC_SENSOR_TYPE, sensor->sensor_type);
}

/* Added for anti_banding. y36721 todo */
int ispv1_set_anti_banding(camera_anti_banding banding)
{
	u32 op = 0;

	print_debug("enter %s", __func__);

	switch (banding) {
	case CAMERA_ANTI_BANDING_OFF:
		op = 0;
		SETREG8(REG_ISP_BANDFILTER_SHORT_EN, 0x1);
		break;

	case CAMERA_ANTI_BANDING_50Hz:
		op = 2;
		SETREG8(REG_ISP_BANDFILTER_SHORT_EN, 0x0);
		break;

	case CAMERA_ANTI_BANDING_60Hz:
		op = 1;
		SETREG8(REG_ISP_BANDFILTER_SHORT_EN, 0x0);
		break;

	case CAMERA_ANTI_BANDING_AUTO:
		/* y36721 todo */
		break;

	default:
		return -1;
	}

	SETREG8(REG_ISP_BANDFILTER_EN, 0x1);
	SETREG8(REG_ISP_BANDFILTER_FLAG, op);

	return 0;
}

int ispv1_get_anti_banding(void)
{
	u32 op = 0;
	camera_anti_banding banding;

	print_debug("enter %s", __func__);

	op = GETREG8(REG_ISP_BANDFILTER_FLAG);

	switch (op) {
	case 0:
		banding = CAMERA_ANTI_BANDING_OFF;
		break;

	case 1:
		banding = CAMERA_ANTI_BANDING_60Hz;
		break;

	case 2:
		banding = CAMERA_ANTI_BANDING_50Hz;
		break;
	default:
		return -1;
	}

	return banding;
}

/* blue,green,red gains */
u16 isp_mwb_gain[CAMERA_WHITEBALANCE_MAX][3] = {
	{0x0000, 0x0000, 0x0000}, /* AWB not care about it */
	{0x012c, 0x0080, 0x0089}, /* INCANDESCENT 2800K */
	{0x00f2, 0x0080, 0x00b9}, /* FLUORESCENT 4200K */
	{0x00a0, 0x00a0, 0x00a0}, /* WARM_FLUORESCENT, y36721 todo */
	{0x00d1, 0x0080, 0x00d2}, /* DAYLIGHT 5000K */
	{0x00b0, 0x0080, 0x00ec}, /* CLOUDY_DAYLIGHT 6500K*/
	{0x00a0, 0x00a0, 0x00a0}, /* TWILIGHT, y36721 todo */
	{0x0168, 0x0080, 0x0060}, /* CANDLELIGHT, 2300K */
};

#if 1
/* Added for awb */
int ispv1_set_awb(camera_white_balance awb_mode)
{
	print_debug("enter %s", __func__);
	/* default is auto, ...... */

#ifdef OVISP_DEBUG_MODE
	return 0;
#endif

	switch (awb_mode) {
	case CAMERA_WHITEBALANCE_AUTO:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);
		/*  Awb mode, should set CT-based AWB */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, 0x1);
		break;

	case CAMERA_WHITEBALANCE_INCANDESCENT:
	case CAMERA_WHITEBALANCE_FLUORESCENT:
	case CAMERA_WHITEBALANCE_WARM_FLUORESCENT:
	case CAMERA_WHITEBALANCE_DAYLIGHT:
	case CAMERA_WHITEBALANCE_CLOUDY_DAYLIGHT:
	case CAMERA_WHITEBALANCE_TWILIGHT:
	case CAMERA_WHITEBALANCE_CANDLELIGHT:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);	/* y36721 fix it */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, awb_mode + 2);
		SETREG16(REG_ISP_AWB_MANUAL_GAIN_BLUE(awb_mode - 1),
			 isp_mwb_gain[awb_mode][0]);
		SETREG16(REG_ISP_AWB_MANUAL_GAIN_GREEN(awb_mode - 1),
			 isp_mwb_gain[awb_mode][1]);
		SETREG16(REG_ISP_AWB_MANUAL_GAIN_RED(awb_mode - 1),
			 isp_mwb_gain[awb_mode][2]);
		break;

	default:
		print_error("unknow awb mode\n");
		return -1;
		break;
	}

	return 0;
}
#else
int ispv1_set_awb(camera_white_balance awb_mode)
{
	print_info("enter %s, awb mode", __func__, awb_mode);
	/* default is auto, ...... */

	switch (awb_mode) {
	case CAMERA_WHITEBALANCE_AUTO:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);
		/*  Awb mode, should set CT-based AWB */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, 0x3);
		break;

	case CAMERA_WHITEBALANCE_INCANDESCENT:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);
		/*  Awb mode, should set CT-based AWB */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, 0x4);
		break;
	case CAMERA_WHITEBALANCE_DAYLIGHT:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);
		/*  Awb mode, should set CT-based AWB */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, 0x5);
		break;

	case CAMERA_WHITEBALANCE_FLUORESCENT:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);
		/*  Awb mode, should set CT-based AWB */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, 0x6);
		break;

	case CAMERA_WHITEBALANCE_CLOUDY_DAYLIGHT:
		SETREG8(REG_ISP_AWB_MANUAL_ENABLE, 0x0);
		/*  Awb mode, should set CT-based AWB */
		SETREG8(REG_ISP_AWB_METHOD_TYPE, 0x7);
		break;

	default:
		print_error("unknow awb mode\n");
		return -1;
		break;
	}

	return 0;
}
#endif

/* Added for sharpness, y36721 todo */
int ispv1_set_sharpness(camera_sharpness sharpness)
{
	print_debug("enter %s", __func__);

	return 0;
}

/* Added for saturation, y36721 todo */
int ispv1_set_saturation(camera_saturation saturation)
{
	print_debug("enter %s, %d", __func__, saturation);
	this_ispdata->saturation = saturation;

	return 0;
}

int ispv1_set_saturation_done(camera_saturation saturation)
{
	print_debug("enter %s, %d", __func__, saturation);

	switch (saturation) {
	case CAMERA_SATURATION_L2:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_SATURATION_ENABLE);
		SETREG8(REG_ISP_SDE_U_SATURATION, 0x10);
		SETREG8(REG_ISP_SDE_V_SATURATION, 0x10);
		break;

	case CAMERA_SATURATION_L1:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_SATURATION_ENABLE);
		SETREG8(REG_ISP_SDE_U_SATURATION, 0x28);
		SETREG8(REG_ISP_SDE_V_SATURATION, 0x28);
		break;

	case CAMERA_SATURATION_H0:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_SATURATION_ENABLE);
		SETREG8(REG_ISP_SDE_U_SATURATION, 0x40);
		SETREG8(REG_ISP_SDE_V_SATURATION, 0x40);
		break;

	case CAMERA_SATURATION_H1:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_SATURATION_ENABLE);
		SETREG8(REG_ISP_SDE_U_SATURATION, 0x58);
		SETREG8(REG_ISP_SDE_V_SATURATION, 0x58);
		break;

	case CAMERA_SATURATION_H2:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_SATURATION_ENABLE);
		SETREG8(REG_ISP_SDE_U_SATURATION, 0x70);
		SETREG8(REG_ISP_SDE_V_SATURATION, 0x70);
		break;

	default:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) & (~ISP_SDE_ENABLE));
		break;
	}

	return 0;
}

int ispv1_set_contrast(camera_contrast contrast)
{
	print_debug("enter %s, %d", __func__, contrast);
	this_ispdata->contrast = contrast;

	return 0;
}

int ispv1_set_contrast_done(camera_contrast contrast)
{
	print_debug("enter %s, %d", __func__, contrast);

	SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
	SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_CONTRAST_ENABLE);

	ispv1_switch_contrast(STATE_PREVIEW, contrast);

	return 0;
}

int ispv1_switch_contrast(camera_state state, camera_contrast contrast)
{
	if (state == STATE_PREVIEW) {
		switch (contrast) {
		case CAMERA_CONTRAST_L2:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_PREVIEW_L2);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_L1:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_PREVIEW_L1);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_H0:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_PREVIEW_H0);
			SETREG8(REG_ISP_TOP5, (GETREG8(REG_ISP_TOP5) | SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_H1:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_PREVIEW_H1);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_H2:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_PREVIEW_H2);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		default:
			print_error("%s, not supported contrast %d", __func__, contrast);
			break;
		}
	} else if (state == STATE_CAPTURE) {
		switch (contrast) {
		case CAMERA_CONTRAST_L2:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_CAPTURE_L2);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_L1:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_CAPTURE_L1);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_H0:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_CAPTURE_H0);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_H1:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_CAPTURE_H1);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		case CAMERA_CONTRAST_H2:
			SETREG8(REG_ISP_SDE_CONTRAST, SDE_CONTRAST_CAPTURE_H2);
			SETREG8(REG_ISP_TOP5, GETREG8(REG_ISP_TOP5) & (~SDE_MANUAL_OFFSET_ENABLE));
			break;

		default:
			print_error("%s, not supported contrast %d", __func__, contrast);
			break;
		}
	}

	return 0;
}

void ispv1_set_fps_lock(int lock)
{
	print_debug("enter %s", __func__);
	fps_lock = lock;
}

void ispv1_change_fps(camera_frame_rate_mode mode)
{
	print_debug("enter :%s, mode :%d", __func__, mode);
	if (fps_lock == true) {
		print_error("fps is locked");
		return;
	}
	if (mode >= CAMERA_FRAME_RATE_MAX)
		print_error("inviable camera_frame_rate_mode: %d", mode);

	if (CAMERA_FRAME_RATE_FIX_MAX == mode) {
		this_ispdata->sensor->fps_min = isp_hw_data.fps_max;
		this_ispdata->sensor->fps_max = isp_hw_data.fps_max;
		ispv1_set_frame_rate(CAMERA_FRAME_RATE_FIX_MAX, this_ispdata->sensor);
	} else if (CAMERA_FRAME_RATE_FIX_MIN == mode) {
		this_ispdata->sensor->fps_min = isp_hw_data.fps_min;
		this_ispdata->sensor->fps_max = isp_hw_data.fps_min;
		ispv1_set_frame_rate(CAMERA_FRAME_RATE_FIX_MIN, this_ispdata->sensor);
	} else if (CAMERA_FRAME_RATE_AUTO == mode) {
		this_ispdata->sensor->fps_min = isp_hw_data.fps_min;
		this_ispdata->sensor->fps_max = isp_hw_data.fps_max;
	}

	this_ispdata->fps_mode = mode;
}

void ispv1_change_max_exposure(camera_sensor *sensor, camera_max_exposrure mode)
{
	u8 frame_index, fps;
	u32 vts;
	u16 max_exposure;

	frame_index = sensor->preview_frmsize_index;
	fps = sensor->frmsize_list[frame_index].fps;
	vts = sensor->frmsize_list[frame_index].vts;

	if (CAMERA_MAX_EXPOSURE_LIMIT == mode)
		max_exposure = (fps * vts / 100 - 14);
	else
		max_exposure = ((fps * vts / sensor->fps) - 14);

	SETREG16(REG_ISP_MAX_EXPOSURE, max_exposure);
	SETREG16(REG_ISP_MAX_EXPOSURE_SHORT, max_exposure);
}

int ispv1_set_scene(camera_scene scene)
{
	u8 target_y_low = scene_target_y_low;
	u8 target_y_high = scene_target_y_high;

#ifdef OVISP_DEBUG_MODE
	return 0;
#endif

	print_debug("enter %s, scene:%d", __func__, scene);

	switch (scene) {
	case CAMERA_SCENE_AUTO:
		print_info("case CAMERA_SCENE_AUTO ");
		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = isp_hw_data.flash_mode;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(this_ispdata->awb_mode);
		break;

	case CAMERA_SCENE_ACTION:
		print_info("case CAMERA_SCENE_ACTION ");
		/*
		 * Reduce max exposure time 1/100s
		 * Pre-focus recommended. (optional)
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_FIX_MAX);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_LIMIT);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_PORTRAIT:
		print_info("case CAMERA_SCENE_PORTRAIT ");
		/*
		 * Pre-tuned color matrix for better skin tone recommended. (optional)
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = isp_hw_data.flash_mode;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x90);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_LANDSPACE:
		print_info("case CAMERA_SCENE_LANDSPACE ");
		/*
		 * Focus mode set to infinity
		 * Manual AWB to daylight (optional)
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x98);
		ispv1_set_focus_mode(CAMERA_FOCUS_INFINITY);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_NIGHT:
		print_info("case CAMERA_SCENE_NIGHT ");
		/*
		 * Increase max exposure time��֡�ʽ��͵�5fps����
		 * Turn off the flash
		 * Turn off the UV adjust
		 * Focus mode set to infinity
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_FIX_MIN);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_DISABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_NIGHT_PORTRAIT:
		print_info("case CAMERA_SCENE_NIGHT_PORTRAIT ");
		/*
		 * Increase max exposure time
		 * Turn off UV adjust
		 * Turn on the flash (optional)
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_FIX_MIN);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_DISABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x90);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_THEATRE:
		print_info("case CAMERA_SCENE_THEATRE ");
		/*
		 * Increase max exposure time��֡�ʽ��͵�5fps����
		 * Turn off the flash
		 * Turn off UV adjust
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_FIX_MIN);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_DISABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x70);
		ispv1_set_focus_mode(CAMERA_FOCUS_INFINITY);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_BEACH:
		print_info("case CAMERA_SCENE_BEACH ");
		/*
		 * Reduce AE target
		 * Manual AWB set to daylight (optional)
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		ispv1_calc_ev(&target_y_low, &target_y_high, -2);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_SNOW:
		print_info("case CAMERA_SCENE_SNOW ");
		/*
		 * Increase AE target
		 * Enable EDR mode
		 * Manual AWB set to daylight (optional)
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		ispv1_calc_ev(&target_y_low, &target_y_high, 2);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_FIREWORKS:
		print_info("case CAMERA_SCENE_FIREWORKS ");
		/*
		 * Increase max exposure time��֡�ʽ��͵�5fps����
		 * Focus mode set to infinity
		 * Turn off the flash
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_FIX_MIN);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_INFINITY);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_CANDLELIGHT:
		print_info("case CAMERA_SCENE_CANDLELIGHT ");
		/*
		 * Increase max exposure time��֡�ʽ��͵�5fps����
		 * Turn off the flash
		 * Turn off UV adjust
		 * AWB set to manual with fixed AWB gain with yellow/warm tone
		 */
		ispv1_change_fps(CAMERA_FRAME_RATE_FIX_MIN);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_DISABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_CONTINUOUS_PICTURE);
		ispv1_set_awb(CAMERA_WHITEBALANCE_CANDLELIGHT);
		break;

	case CAMERA_SCENE_FLOWERS:
		print_info("case CAMERA_SCENE_FLOWER ");

		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		this_ispdata->flash_mode = CAMERA_FLASH_OFF;
		ispv1_set_focus_mode(CAMERA_FOCUS_MACRO);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		break;

	case CAMERA_SCENE_SUNSET:
	case CAMERA_SCENE_STEADYPHOTO:
	case CAMERA_SCENE_SPORTS:
	case CAMERA_SCENE_BARCODE:
	default:
		print_info("This scene not supported yet. ");
		ispv1_change_fps(CAMERA_FRAME_RATE_AUTO);
		ispv1_change_max_exposure(this_ispdata->sensor, CAMERA_MAX_EXPOSURE_RESUME);
		SETREG8(REG_ISP_TARGET_Y_LOW, target_y_low);
		SETREG8(REG_ISP_TARGET_Y_HIGH, target_y_high);
		this_ispdata->flash_mode = isp_hw_data.flash_mode;
		this_ispdata->scene = CAMERA_SCENE_AUTO;
		SETREG8(REG_ISP_UV_ADJUST, UV_ADJUST_ENABLE);
		SETREG8(REG_ISP_UV_SATURATION, 0x80);
		ispv1_set_focus_mode(CAMERA_FOCUS_AUTO);
		ispv1_set_awb(CAMERA_WHITEBALANCE_AUTO);
		goto out;
	}

	this_ispdata->scene = scene;

out:
	return 0;
}

int ispv1_set_brightness(camera_brightness brightness)
{
	print_debug("enter %s, %d", __func__, brightness);
	this_ispdata->brightness = brightness;

	return 0;
}

int ispv1_set_brightness_done(camera_brightness brightness)
{
	print_debug("enter %s, %d", __func__, brightness);

	switch (brightness) {
	case CAMERA_BRIGHTNESS_L2:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_BRIGHTNESS_ENABLE);
		SETREG8(REG_ISP_SDE_SIGN_SET, GETREG8(REG_ISP_SDE_SIGN_SET) | ISP_BRIGHTNESS_SIGN_NEGATIVE);
		break;

	case CAMERA_BRIGHTNESS_L1:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_BRIGHTNESS_ENABLE);
		SETREG8(REG_ISP_SDE_SIGN_SET, GETREG8(REG_ISP_SDE_SIGN_SET) | ISP_BRIGHTNESS_SIGN_NEGATIVE);
		break;

	case CAMERA_BRIGHTNESS_H0:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_BRIGHTNESS_ENABLE);
		SETREG8(REG_ISP_SDE_SIGN_SET, GETREG8(REG_ISP_SDE_SIGN_SET) & (~ISP_BRIGHTNESS_SIGN_NEGATIVE));
		break;

	case CAMERA_BRIGHTNESS_H1:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_BRIGHTNESS_ENABLE);
		SETREG8(REG_ISP_SDE_SIGN_SET, GETREG8(REG_ISP_SDE_SIGN_SET) & (~ISP_BRIGHTNESS_SIGN_NEGATIVE));
		break;

	case CAMERA_BRIGHTNESS_H2:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, GETREG8(REG_ISP_SDE_CTRL) | ISP_BRIGHTNESS_ENABLE);
		SETREG8(REG_ISP_SDE_SIGN_SET, GETREG8(REG_ISP_SDE_SIGN_SET) & (~ISP_BRIGHTNESS_SIGN_NEGATIVE));
		break;

	default:
		print_error("%s, not supported brightness %d", __func__, brightness);
		break;
	}

	ispv1_switch_brightness(STATE_PREVIEW, brightness);

	return 0;
}

int ispv1_switch_brightness(camera_state state, camera_brightness brightness)
{
	if (state == STATE_PREVIEW) {
		switch (brightness) {
		case CAMERA_BRIGHTNESS_L2:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_PREVIEW_L2);
			break;

		case CAMERA_BRIGHTNESS_L1:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_PREVIEW_L1);
			break;

		case CAMERA_BRIGHTNESS_H0:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_PREVIEW_H0);
			break;

		case CAMERA_BRIGHTNESS_H1:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_PREVIEW_H1);
			break;

		case CAMERA_BRIGHTNESS_H2:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_PREVIEW_H2);
			break;

		default:
			break;
		}
	} else if (state == STATE_CAPTURE) {
		switch (brightness) {
		case CAMERA_BRIGHTNESS_L2:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_CAPTURE_L2);
			break;

		case CAMERA_BRIGHTNESS_L1:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_CAPTURE_L1);
			break;

		case CAMERA_BRIGHTNESS_H0:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_CAPTURE_H0);
			break;

		case CAMERA_BRIGHTNESS_H1:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_CAPTURE_H1);
			break;

		case CAMERA_BRIGHTNESS_H2:
			SETREG8(REG_ISP_SDE_BRIGHTNESS, SDE_BRIGHTNESS_CAPTURE_H2);
			break;

		default:
			break;
		}
	}

	return 0;
}

int ispv1_set_effect(camera_effects effect)
{
	print_debug("enter %s, %d", __func__, effect);
	this_ispdata->effect = effect;

	return 0;
}

int ispv1_set_effect_done(camera_effects effect)
{
	print_debug("enter %s, %d", __func__, effect);

	switch (effect) {
	case CAMERA_EFFECT_NONE:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, ISP_CONTRAST_ENABLE | ISP_BRIGHTNESS_ENABLE | ISP_SATURATION_ENABLE);
		break;

	case CAMERA_EFFECT_MONO:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, ISP_MONO_EFFECT_ENABLE);
		break;

	case CAMERA_EFFECT_NEGATIVE:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, ISP_NEGATIVE_EFFECT_ENABLE);
		break;

	case CAMERA_EFFECT_SEPIA:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) | ISP_SDE_ENABLE);
		SETREG8(REG_ISP_SDE_CTRL, ISP_FIX_U_ENABLE | ISP_FIX_V_ENABLE);
		SETREG8(REG_ISP_SDE_U_REG, 0x30);
		SETREG8(REG_ISP_SDE_V_REG, 0xb0);
		break;

	default:
		SETREG8(REG_ISP_TOP2, GETREG8(REG_ISP_TOP2) & (~ISP_SDE_ENABLE));
		break;
	}

	return 0;
}

int ispv1_set_effect_saturation_done(camera_effects effect, camera_saturation saturation)
{
	if(effect == CAMERA_EFFECT_SEPIA) {
		ispv1_set_effect_done(effect);
	} else {
		ispv1_set_effect_done(effect);
		ispv1_set_saturation_done(saturation);
	}
	return 0;
}

/*
 * Added for hue, y36721 todo
 * flag: if 1 is on, 0 is off, default is off
 */
int ispv1_set_hue(int flag)
{
	print_debug("enter %s", __func__);

	return 0;
}

/*
 * before start_preview or start_capture, it should be called to update size information
 * please see ISP manual or software manual.
 */
int ispv1_update_LENC_scale(u32 inwidth, u32 inheight)
{
	u32 scale;

	print_debug("enter %s", __func__);

	scale = (0x100000 * 3) / inwidth;
	SETREG16(REG_ISP_LENC_BRHSCALE, scale);

	scale = (0x100000 * 3) / inheight;
	SETREG16(REG_ISP_LENC_BRVSCALE, scale);

	scale = (0x100000 * 4) / inwidth;
	SETREG16(REG_ISP_LENC_GHSCALE, scale);

	scale = (0x80000 * 4) / inheight;
	SETREG16(REG_ISP_LENC_GVSCALE, scale);

	return 0;

}

/*
 * Related to sensor.
 */
int ispv1_init_LENC(u8 *lensc_param)
{
	u32 loopi;
	u8 *param;

	print_debug("enter %s", __func__);

	assert(lensc_param);

	/* set long exposure */
	param = lensc_param;
	for (loopi = 0; loopi < LENS_CP_ARRAY_BYTES; loopi++)
		SETREG8(REG_ISP_LENS_CP_ARRAY_LONG + loopi, *param++);

	/* set short exposure, just set same with long exposure, y36721 todo */
	param = lensc_param;
	for (loopi = 0; loopi < LENS_CP_ARRAY_BYTES; loopi++)
		SETREG8(REG_ISP_LENS_CP_ARRAY_SHORT + loopi, *param++);

	return 0;
}

/*
 * Related to sensor.
 */
int ispv1_init_CCM(u16 *ccm_param)
{
	u32 loopi;
	u16 *param;

	print_debug("enter %s ", __func__);

	assert(ccm_param);

	param = ccm_param;
	for (loopi = 0; loopi < CCM_MATRIX_ARRAY_SIZE16; loopi++) {
		SETREG8(REG_ISP_CCM_MATRIX + loopi * 2, (*param & 0xff00) >> 8);
		SETREG8(REG_ISP_CCM_MATRIX + loopi * 2 + 1, *param & 0xff);
		param++;
	}

	return 0;
}

/*
 * Related to sensor.
 */
int ispv1_init_AWB(u8 *awb_param)
{
	u32 loopi;
	u8 *param;

	print_debug("enter %s", __func__);

	assert(awb_param);

	param = awb_param;
	for (loopi = 0; loopi < AWB_CTRL_ARRAY_BYTES; loopi++)
		SETREG8(REG_ISP_AWB_CTRL + loopi, *param++);

	SETREG8(REG_ISP_CCM_CENTERCT_THRESHOLDS, *param++);
	SETREG8(REG_ISP_CCM_CENTERCT_THRESHOLDS + 1, *param++);
	SETREG8(REG_ISP_CCM_LEFTCT_THRESHOLDS, *param++);
	SETREG8(REG_ISP_CCM_LEFTCT_THRESHOLDS + 1, *param++);
	SETREG8(REG_ISP_CCM_RIGHTCT_THRESHOLDS, *param++);
	SETREG8(REG_ISP_CCM_RIGHTCT_THRESHOLDS + 1, *param++);

	for (loopi = 0; loopi < LENS_CT_THRESHOLDS_SIZE16; loopi++) {
		SETREG8(REG_ISP_LENS_CT_THRESHOLDS + loopi * 2, *param++);
		SETREG8(REG_ISP_LENS_CT_THRESHOLDS + loopi * 2 + 1, *param++);
	}

	return 0;
}

/* Added for binning correction, y36721 todo */
int ispv1_init_BC(int binningMode, int mirror, int filp)
{
	print_debug("enter %s", __func__);

	return 0;
}

/* Added for defect_pixel_correction, y36721 todo */
int ispv1_init_DPC(int bWhitePixel, int bBlackPixel)
{
	print_debug("enter %s", __func__);

	return 0;
}

/*
 * Added for raw DNS, y36721 todo
 * flag: if 1 is on, 0 is off, default is on
 */
int ispv1_init_rawDNS(int flag)
{
	print_debug("enter %s", __func__);

	return 0;
}

/*
 * Added for uv DNS, y36721 todo
 * flag: if 1 is on, 0 is off, default is on
 */
int ispv1_init_uvDNS(int flag)
{
	print_debug("enter %s", __func__);

	return 0;
}

/*
 * Added for GbGr DNS, y36721 todo
 * level: if >0 is on, 0 is off, default is 6
 * 16: keep full Gb/Gr difference as resolution;
 * 8: remove half Gb/Gr difference;
 * 0: remove all Gb/Gr difference;
 */
int ispv1_init_GbGrDNS(int level)
{
	print_debug("enter %s", __func__);

	return 0;
}

/*
 * Added for GRB Gamma, y36721 todo
 * flag: if 1 is on, 0 is off, default is on, 0x65004 bit[5]
 */
int ispv1_init_RGBGamma(int flag)
{
	print_debug("enter %s", __func__);

	return 0;
}

/*
 * For not OVT sensors,  MCU won't write exposure and gain back to sensor directly.
 * In this case, exposure and gain need Host to handle.
 */
void ispv1_cmd_id_do_ecgc(void)
{
	u16 gain;
	u32 exposure;

	print_debug("enter %s, cmd id 0x%x", __func__, isp_hw_data.aec_cmd_id);

	if (CMD_WRITEBACK_EXPO_GAIN == isp_hw_data.aec_cmd_id) {
		exposure = get_writeback_expo();
		if (this_ispdata->sensor->set_exposure)
			this_ispdata->sensor->set_exposure(exposure);

		gain = get_writeback_gain();
		if (this_ispdata->sensor->set_gain)
			this_ispdata->sensor->set_gain(gain);

		print_info("expo 0x%x, gain 0x%x, currentY 0x%x", exposure, gain, get_current_y());
	} else if (CMD_WRITEBACK_EXPO == isp_hw_data.aec_cmd_id) {
		exposure = get_writeback_expo();
		if (this_ispdata->sensor->set_exposure)
			this_ispdata->sensor->set_exposure(exposure);

		print_info("expo 0x%x, currentY 0x%x", exposure, get_current_y());
	} else if (CMD_WRITEBACK_GAIN == isp_hw_data.aec_cmd_id) {
		gain = get_writeback_gain();
		if (this_ispdata->sensor->set_gain)
			this_ispdata->sensor->set_gain(gain);

		print_info("gain 0x%x, currentY 0x%x", gain, get_current_y());
	} else {
		print_error("%s:unknow cmd id", __func__);
	}
}

static int frame_rate_level;
static camera_frame_rate_state fps_state = CAMERA_FRAME_RATE_HIGH;

int ispv1_get_frame_rate_level()
{
	return frame_rate_level;
}

void ispv1_set_frame_rate_level(int level)
{
	frame_rate_level = level;
	print_info("%s: level %d", __func__, level);
}

camera_frame_rate_state ispv1_get_frame_rate_state(void)
{
	return fps_state;
}

void ispv1_set_frame_rate_state(camera_frame_rate_state state)
{
	fps_state = state;
}

static int ispv1_set_frame_rate(camera_frame_rate_mode mode, camera_sensor *sensor)
{
	int frame_rate_level = 0;
	u16 vts, fullfps, fps;
	u32 max_fps, min_fps;

	fullfps = sensor->frmsize_list[sensor->preview_frmsize_index].fps;
	if (CAMERA_FRAME_RATE_FIX_MAX == mode) {
		max_fps = (isp_hw_data.fps_max > fullfps) ? fullfps : isp_hw_data.fps_max;
		min_fps = max_fps;
		frame_rate_level = 0;
	} else if (CAMERA_FRAME_RATE_FIX_MIN == mode) {
		min_fps = (isp_hw_data.fps_min < fullfps) ? isp_hw_data.fps_min : fullfps;
		max_fps = (isp_hw_data.fps_max > fullfps) ? fullfps : isp_hw_data.fps_max;
		frame_rate_level = max_fps - min_fps;
	}

	fps = fullfps - frame_rate_level;

	sensor->fps = fps;

	/* rules: vts1*fps1 = vts2*fps2 */
	vts = sensor->frmsize_list[sensor->preview_frmsize_index].vts;
	vts = vts * fullfps / fps;

	if (sensor->set_vts) {
		sensor->set_vts(vts);
	} else {
		print_error("set_vts null");
		goto error;
	}

	SETREG16(REG_ISP_MAX_EXPOSURE, (vts - 14));
	ispv1_set_frame_rate_level(frame_rate_level);
	return 0;
error:
	return -1;
}

static bool ispv1_change_frame_rate(
	camera_frame_rate_state *state, camera_frame_rate_dir direction, camera_sensor *sensor)
{
	int frame_rate_level = ispv1_get_frame_rate_level();
	u16 vts, fullfps, fps;
	bool level_changed = false;
	u32 max_fps, min_fps;
	u32 max_level;

	fullfps = sensor->frmsize_list[sensor->preview_frmsize_index].fps;
	max_fps = (isp_hw_data.fps_max > fullfps) ? fullfps : isp_hw_data.fps_max;
	min_fps = (isp_hw_data.fps_min < fullfps) ? isp_hw_data.fps_min : fullfps;
	if (min_fps > max_fps)
		min_fps = max_fps;
	max_level = max_fps - min_fps;

	print_debug("%s: state  %d, frame_rate_level %d", __func__, *state, frame_rate_level);

	if (*state == CAMERA_EXPO_PRE_REDUCE) {
		level_changed = true;
		/* desired level should go to FRAME_RATE_HIGH level, state go to CAMERA_FRAME_RATE_HIGH*/
		frame_rate_level -= max_level;
		*state = CAMERA_FRAME_RATE_HIGH;
		goto framerate_set_done;
	}

	if (CAMERA_FRAME_RATE_DOWN == direction) {
		if (frame_rate_level >= max_level) {
			print_debug("Has arrival max frame_rate level");
			return true;
		} else {
			frame_rate_level += max_level;
			*state = CAMERA_FRAME_RATE_LOW;
			level_changed = true;
		}
	} else if (CAMERA_FRAME_RATE_UP == direction) {
		if (0 == frame_rate_level) {
			print_debug("Has arrival min frame_rate level");
			return true;
		} else {
			frame_rate_level -= max_level;
			*state = CAMERA_FRAME_RATE_HIGH;
			level_changed = true;
		}
	}

framerate_set_done:
	fps = fullfps - frame_rate_level;
	if ((fps > sensor->fps_max) || (fps < sensor->fps_min)) {
		print_info("auto fps:%d", fps);
		return true;
	}

	if (true == level_changed) {
		if ((sensor->fps_min <= fps) && (sensor->fps_max >= fps)) {
			sensor->fps = fps;
		} else if (sensor->fps_min > fps) {
			frame_rate_level = 0;
			sensor->fps = fullfps;
			fps = fullfps;
		} else {
			print_error("can't do auto fps, level:%d, cur_fps:%d, tar_fps:%d, ori_fps:%d, max_fps:%d, min_fps:%d",
				frame_rate_level, sensor->fps, fps, fullfps, sensor->fps_max, sensor->fps_min);
			goto error_out;
		}

		/* rules: vts1*fps1 = vts2*fps2 */
		vts = sensor->frmsize_list[sensor->preview_frmsize_index].vts;
		vts = vts * fullfps / fps;

		if ((vts - 14) < (get_writeback_expo() / 0x10)) {
			SETREG16(REG_ISP_MAX_EXPOSURE, (vts - 14));
			print_warn("current expo too large");
			*state = CAMERA_EXPO_PRE_REDUCE;
			return true;
		}

		if (sensor->set_vts) {
			sensor->set_vts(vts);
		} else {
			print_error("set_vts null");
			goto error_out;
		}
		SETREG16(REG_ISP_MAX_EXPOSURE, (vts - 14));
		ispv1_set_frame_rate_level(frame_rate_level);
	}
	return true;

error_out:
	return false;
}

/* #define PLATFORM_TYPE_PAD_S10 */
#define BOARD_ID_CS_U9510		0x67
#define BOARD_ID_CS_U9510E		0x66
#define BOARD_ID_CS_T9510E		0x06
awb_gain_t flash_platform_awb[FLASH_PLATFORM_MAX] =
{
	{0xc8, 0x80, 0x80, 0x104}, /* U9510 */
	{0xd6, 0x80, 0x80, 0x104}, /* U9510E/T9510E, recording AWB is 0xd0,0xfc, change a little */
	{0xd0, 0x80, 0x80, 0x100}, /* s10 */
};

static void ispv1_cal_capture_awb(
	awb_gain_t *preview_awb, awb_gain_t *flash_awb, awb_gain_t *capture_awb,
	u32 weight_env, u32 weight_flash)
{
	capture_awb->b_gain = (weight_flash * flash_awb->b_gain + weight_env * preview_awb->b_gain) / 0x100;
	capture_awb->gb_gain = (weight_flash * flash_awb->gb_gain + weight_env * preview_awb->gb_gain) / 0x100;
	capture_awb->gr_gain = (weight_flash * flash_awb->gr_gain + weight_env * preview_awb->gr_gain) / 0x100;
	capture_awb->r_gain = (weight_flash * flash_awb->r_gain + weight_env * preview_awb->r_gain) / 0x100;
}

void ispv1_get_wb_value(awb_gain_t *awb)
{
	GETREG16(MANUAL_AWB_GAIN_B, awb->b_gain);
	GETREG16(MANUAL_AWB_GAIN_GB, awb->gb_gain);
	GETREG16(MANUAL_AWB_GAIN_GR, awb->gr_gain);
	GETREG16(MANUAL_AWB_GAIN_R, awb->r_gain);
}

void ispv1_set_wb_value(awb_gain_t *awb)
{
	SETREG16(MANUAL_AWB_GAIN_B, awb->b_gain);
	SETREG16(MANUAL_AWB_GAIN_GB, awb->gb_gain);
	SETREG16(MANUAL_AWB_GAIN_GR, awb->gr_gain);
	SETREG16(MANUAL_AWB_GAIN_R, awb->r_gain);
}

static void ispv1_cal_weight(
	u32 lum_env, u32 lum_flash,
	u32 *weight_env, u32 *weight_flash)
{
	if ((lum_flash + lum_env) != 0) {
		*weight_env = 0x100 * lum_env / (lum_flash + lum_env);
		*weight_flash = 0x100 * lum_flash / (lum_flash + lum_env);
	} else {
		*weight_env = 0x100;
		*weight_flash = 0;
	}
}

static void ispv1_cal_flash_awb(
	u32 weight_env, u32 weight_flash,
	awb_gain_t *preview_awb, awb_gain_t *preflash_awb, awb_gain_t *flash_awb)
{
	if (weight_flash != 0) {
		flash_awb->b_gain = abs((int)preflash_awb->b_gain * 0x100 - (int)preview_awb->b_gain * weight_env) / weight_flash;
		flash_awb->gb_gain = abs((int)preflash_awb->gb_gain * 0x100 - (int)preview_awb->gb_gain * weight_env) / weight_flash;
		flash_awb->gr_gain = abs((int)preflash_awb->gr_gain * 0x100 - (int)preview_awb->gr_gain * weight_env) / weight_flash;
		flash_awb->r_gain = abs((int)preflash_awb->r_gain * 0x100 - (int)preview_awb->r_gain * weight_env) / weight_flash;
	} else {
		flash_awb->b_gain = preview_awb->b_gain;
		flash_awb->gb_gain = preview_awb->gb_gain;
		flash_awb->gr_gain = preview_awb->gr_gain;
		flash_awb->r_gain = preview_awb->r_gain;
	}
}

static bool ispv1_check_awb_match(awb_gain_t *flash_awb, awb_gain_t *led0, awb_gain_t *led1, u32 weight_env)
{
	awb_gain_t diff_led0, diff_led1;
	bool ret = true;

	/* if calculated flash_awb is differ a lot from preset_flash_awb, use preset_flash_awb. */
	diff_led0.b_gain = abs((int)led0->b_gain - (int)flash_awb->b_gain);
	diff_led0.r_gain = abs((int)led0->r_gain - (int)flash_awb->r_gain);

	diff_led1.b_gain = abs((int)led1->b_gain - (int)flash_awb->b_gain);
	diff_led1.r_gain = abs((int)led1->r_gain - (int)flash_awb->r_gain);

	if ((diff_led0.b_gain >= led0->b_gain / 3 || diff_led0.r_gain >= led0->r_gain / 3) &&
		(diff_led1.b_gain >= led1->b_gain / 3 || diff_led1.r_gain >= led1->r_gain / 3)) {
		ret = false;
		print_info("differ a lot, cal awb[B 0x%x, R 0x%x]", flash_awb->b_gain, flash_awb->r_gain);
	}

	return ret;
}

static void ispv1_cal_ratio_lum(
	aec_data_t *preview_ae, aec_data_t *preflash_ae,
	aec_data_t *capflash_ae, u32 *preview_ratio_lum, flash_weight_t *weight)
{
	aec_data_t ratio_ae;
	int delta_lum, delta_lum_max, delta_lum_sum;
	u32 ratio = 0x100;

	print_debug("preview:[0x%x,0x%x,lum 0x%x,lumMax 0x%x,lumSum 0x%x]; preflash:[0x%x,0x%x,lum 0x%x,LumMax 0x%x,lumSum 0x%x]",
		preview_ae->gain, preview_ae->expo, preview_ae->lum, preview_ae->lum_max, preview_ae->lum_sum,
		preflash_ae->gain, preflash_ae->expo, preflash_ae->lum, preflash_ae->lum_max, preflash_ae->lum_sum);

	ratio = ratio * (preflash_ae->gain * preflash_ae->expo) / (preview_ae->gain * preview_ae->expo);
	if (ratio != 0) {
		ratio_ae.lum = preview_ae->lum * ratio / 0x100;
		ratio_ae.lum_max = preview_ae->lum_max * ratio / 0x100;
		ratio_ae.lum_sum = preview_ae->lum_sum * ratio / 0x100;
	} else {
		ratio_ae.lum = 0;
		ratio_ae.lum_max = 0;
		ratio_ae.lum_sum = 0;
	}

	/* delta_lum is lum of env part */
	delta_lum = preflash_ae->lum - ratio_ae.lum;
	if (delta_lum < 0)
		delta_lum = 0;

	delta_lum_max = preflash_ae->lum_max - ratio_ae.lum_max;
	if (delta_lum_max < 0)
		delta_lum_max = 0;

	delta_lum_sum = preflash_ae->lum_sum - ratio_ae.lum_sum;
	if (delta_lum_sum < 0)
		delta_lum_sum = 0;

	capflash_ae->lum = FLASH_CAP2PRE_RATIO * delta_lum + ratio_ae.lum;
	capflash_ae->lum_max = FLASH_CAP2PRE_RATIO * delta_lum_max + ratio_ae.lum_max;
	capflash_ae->lum_sum = FLASH_CAP2PRE_RATIO * delta_lum_sum + ratio_ae.lum_sum;

	/* if low light, we will use lum_sum to calculate weight for accuracy. */
	if (preflash_ae->lum < FLASH_PREFLASH_LOWLIGHT_TH) {
		ispv1_cal_weight(ratio_ae.lum_sum, preflash_ae->lum_sum - ratio_ae.lum_sum, &(weight->preflash_env), &(weight->preflash_flash));
		ispv1_cal_weight(ratio_ae.lum_sum, capflash_ae->lum_sum - ratio_ae.lum_sum, &(weight->capflash_env), &(weight->capflash_flash));
	} else {
		ispv1_cal_weight(ratio_ae.lum, preflash_ae->lum - ratio_ae.lum, &(weight->preflash_env), &(weight->preflash_flash));
		ispv1_cal_weight(ratio_ae.lum, capflash_ae->lum - ratio_ae.lum, &(weight->capflash_env), &(weight->capflash_flash));
	}

	/* if calculated capture flash lum is zero, set it as 1 to avoid zero divide panic. */
	if (capflash_ae->lum == 0)
		capflash_ae->lum = 1;

	*preview_ratio_lum = ratio_ae.lum;
}

u32 ispv1_cal_ratio_factor(aec_data_t *preview_ae, aec_data_t *preflash_ae, aec_data_t *capflash_ae, 
	u32 target_y_low)
{
	u32 ratio_factor = 0x100;
	u32 temp_ratio = 0x100;
	u32 capture_target_lum;

	if (capflash_ae->lum != 0) {
		if (preview_ae->lum < target_y_low) {
			capture_target_lum = DEFAULT_TARGET_Y_FLASH;
			ratio_factor = ratio_factor * capture_target_lum / capflash_ae->lum;
			capflash_ae->lum_max = capflash_ae->lum_max * ratio_factor / 0x100;
			if (capflash_ae->lum_max > FLASH_CAP_RAW_OVER_EXPO) {
				/* decrease capflash max lum to  FLASH_CAP_RAW_OVER_EXPO */
				temp_ratio = 0x100 * FLASH_CAP_RAW_OVER_EXPO / capflash_ae->lum_max;
				ratio_factor = ratio_factor * temp_ratio / 0x100;
				capflash_ae->lum_max = capflash_ae->lum_max * temp_ratio / 0x100;
				print_info("%s, capflash lum_max 0x%x, temp_ratio 0x%x, new ratio_factor 0x%x",
					__func__, capflash_ae->lum_max, temp_ratio, ratio_factor);
			}
		} else {
			capture_target_lum = preview_ae->lum;
			ratio_factor = ratio_factor * capture_target_lum / capflash_ae->lum;
		}
	} else {
		ratio_factor = ISP_EXPOSURE_RATIO_MAX;
	}

	return ratio_factor;
}

void ispv1_save_aecawb_step(aecawb_step_t *step)
{
	step->stable_range0 = GETREG8(REG_ISP_UNSTABLE2STABLE_RANGE);
	step->stable_range1 = GETREG8(REG_ISP_STABLE2UNSTABLE_RANGE);
	step->fast_step = GETREG8(REG_ISP_FAST_STEP);
	step->slow_step = GETREG8(REG_ISP_SLOW_STEP);
	step->awb_step = GETREG8(REG_ISP_AWB_STEP);
}

void ispv1_config_aecawb_step(bool flash_mode, aecawb_step_t *step)
{
	if (flash_mode == true) {
		SETREG8(REG_ISP_UNSTABLE2STABLE_RANGE, (DEFAULT_FLASH_AEC_FAST_STEP / 4));
		SETREG8(REG_ISP_STABLE2UNSTABLE_RANGE, DEFAULT_FLASH_AEC_FAST_STEP);
		SETREG8(REG_ISP_FAST_STEP, DEFAULT_FLASH_AEC_FAST_STEP);
		SETREG8(REG_ISP_SLOW_STEP, DEFAULT_FLASH_AEC_SLOW_STEP);
		SETREG8(REG_ISP_AWB_STEP, DEFAULT_FLASH_AWB_STEP); //awb step
	} else {
		SETREG8(REG_ISP_UNSTABLE2STABLE_RANGE, step->stable_range0);
		SETREG8(REG_ISP_STABLE2UNSTABLE_RANGE, step->stable_range1);
		SETREG8(REG_ISP_FAST_STEP, step->fast_step);
		SETREG8(REG_ISP_SLOW_STEP, step->slow_step);
		SETREG8(REG_ISP_AWB_STEP, step->awb_step); //awb step
	}
}

/* check if current env AE status is need auto-flash. */
bool ae_is_need_flash(camera_sensor *sensor, aec_data_t *ae_data, u32 target_y_low)
{
	u32 frame_index = sensor->preview_frmsize_index;
	u32 compare_gain = FLASH_TRIGGER_GAIN;
	bool binning;
	bool ret;

	binning = sensor->frmsize_list[frame_index].binning;
	if (binning == false)
		compare_gain *= 2;

	if (ae_data->lum <= (target_y_low * FLASH_TRIGGER_LUM_RATIO / 0x100) ||
		ae_data->gain >= compare_gain)
		ret = true;
	else
		ret = false;

	return ret;
}

static inline void set_awbtest_policy(FLASH_AWBTEST_POLICY action)
{
	isp_hw_data.awb_test = action;
}

static inline FLASH_AWBTEST_POLICY get_awbtest_policy(void)
{
	return isp_hw_data.awb_test;
}

void ispv1_check_flash_prepare(void)
{
	camera_sensor *sensor = this_ispdata->sensor;

	flash_platform_t type;
	int boardid = 0;

	awb_gain_t *preset_awb = &(isp_hw_data.flash_preset_awb);
	awb_gain_t *led_awb0 = &(isp_hw_data.led_awb[0]);
	awb_gain_t *led_awb1 = &(isp_hw_data.led_awb[1]);
	FLASH_AWBTEST_POLICY action;

	if (FOCUS_STATE_CAF_RUNNING == get_focus_state() ||
		FOCUS_STATE_CAF_DETECTING == get_focus_state() ||
		FOCUS_STATE_AF_RUNNING == get_focus_state()) {
		print_debug("enter %s, must stop focus, before turn on preflash. ", __func__);
		ispv1_auto_focus(FOCUS_STOP);
	}

	/* enlarge AEC/AGC step to make AEC/AGC stable quickly. */
	ispv1_config_aecawb_step(true, &isp_hw_data.aecawb_step);

	/* get platform type */
	#ifdef PLATFORM_TYPE_PAD_S10
		type = FLASH_PLATFORM_S10;
	#else
		boardid = get_boardid();
		print_info("%s : boardid=0x%x.", __func__, boardid);

		/* if unknow board ID, should use flash params as X9510E */
		if (boardid == BOARD_ID_CS_U9510E || boardid == BOARD_ID_CS_T9510E)
			type = FLASH_PLATFORM_9510E;
		else if (boardid == BOARD_ID_CS_U9510)
			type = FLASH_PLATFORM_U9510;
		else if(product_type("U9508"))
			type = FLASH_PLATFORM_U9508;
		else
			type = FLASH_PLATFORM_9510E;
	#endif

	if(sensor->get_flash_awb != NULL)
		sensor->get_flash_awb(type, preset_awb);
	else
		memcpy(preset_awb, &flash_platform_awb[type], sizeof(awb_gain_t));

	if (isp_hw_data.flash_type == U9508_FLASH_TYPE_OSRAM) {
		preset_awb->b_gain = preset_awb->gr_gain;
		preset_awb->gb_gain = 0x80;
		preset_awb->gr_gain = 0x80;
		preset_awb->r_gain = preset_awb->r_gain;

		action = FLASH_AWBTEST_POLICY_FIXED;
	} else if (isp_hw_data.flash_type == U9508_FLASH_TYPE_EVERLIGHT) {
		preset_awb->b_gain = preset_awb->b_gain;
		preset_awb->gb_gain = 0x80;
		preset_awb->gr_gain = 0x80;
		preset_awb->r_gain = preset_awb->gb_gain;

		action = FLASH_AWBTEST_POLICY_FIXED;
	} else if (preset_awb->gb_gain == 0x80 && preset_awb->gr_gain == 0x80) {
		action = FLASH_AWBTEST_POLICY_FIXED;
	} else {
		led_awb0->b_gain = preset_awb->b_gain;
		led_awb0->gb_gain = 0x80;
		led_awb0->gr_gain = 0x80;
		led_awb0->r_gain = preset_awb->gb_gain;

		led_awb1->b_gain = preset_awb->gr_gain;
		led_awb1->gb_gain = 0x80;
		led_awb1->gr_gain = 0x80;
		led_awb1->r_gain = preset_awb->r_gain;

		preset_awb->b_gain = led_awb0->b_gain;
		preset_awb->gb_gain = 0x80;
		preset_awb->gr_gain = 0x80;
		preset_awb->r_gain = led_awb0->r_gain;

		action = FLASH_AWBTEST_POLICY_FREEGO;
	}

	set_awbtest_policy(action);

	/* If clearly know led type, need do noting, else will do someting. */
	if (action == FLASH_AWBTEST_POLICY_FIXED) {
		return;
	}

	ispv1_set_awb_mode(MANUAL_AWB);
	ispv1_set_wb_value(preset_awb);

	print_info("unsure flash led AWB, preset_awb[B 0x%x, R 0x%x]", preset_awb->b_gain, preset_awb->r_gain);
}

void ispv1_check_flash_exit(void)
{
	ispv1_config_aecawb_step(false, &isp_hw_data.aecawb_step);
}
static void ispv1_poll_flash_lum(void)
{
	static u8 frame_count;
	static u8 frozen_frame;
	k3_isp_data *ispdata = this_ispdata;
	camera_sensor *sensor = ispdata->sensor;

	u32 volatile cur_lum;

	aec_data_t *preview_ae = &(isp_hw_data.preview_ae);
	aec_data_t *preflash_ae = &(isp_hw_data.preflash_ae);
	aec_data_t capflash_ae;
	u32 preview_ratio_lum;

	awb_gain_t *preview_awb = &(isp_hw_data.preview_awb);
	awb_gain_t *preset_awb = &(isp_hw_data.flash_preset_awb);
	awb_gain_t flash_awb = {0x80, 0x80, 0x80, 0x80};
	awb_gain_t capture_awb;

	static awb_gain_t last_awb;
	awb_gain_t cur_awb;
	awb_gain_t diff_awb;
	awb_gain_t *led_awb0 = &(isp_hw_data.led_awb[0]);
	awb_gain_t *led_awb1 = &(isp_hw_data.led_awb[1]);

	flash_weight_t weight;
	bool match_result;
	bool need_flash;
	bool low_ct;

	u8 target_y_low = GETREG8(REG_ISP_TARGET_Y_LOW);
	u8 over_expo_th = GETREG8(REG_ISP_TARGET_Y_HIGH) + DEFAULT_FLASH_AEC_SLOW_STEP;

	u32 unit_area;

	cur_lum = get_current_y();
	ispv1_get_wb_value(&cur_awb);

	/* stage 1: skip first 2 frames */
	if (++frame_count <= 2) {
		if (get_awbtest_policy() == FLASH_AWBTEST_POLICY_FIXED) {
			ispv1_set_aecagc_mode(AUTO_AECAGC);
			ispv1_set_awb_mode(AUTO_AWB);
		} else {
			ispv1_set_aecagc_mode(AUTO_AECAGC);

			/*
			 * first frame set manual AWB and init WB value again to ensure it take effect;
			 * second frame set Auto awb.
			 */
			if (frame_count == 1) {
				ispv1_set_awb_mode(MANUAL_AWB);
				ispv1_set_wb_value(preset_awb);
			} else if (frame_count == 2)
				ispv1_set_awb_mode(AUTO_AWB);
		}
		return;
	}

	/* stage 2: FLASH_TESTING: if aec/agc suitable or frame_count larger than threshold, should go to FLASH_FROZEN. */
	if ((ispdata->flash_flow == FLASH_TESTING) &&
		(cur_lum <= over_expo_th || frame_count >= FLASH_TEST_MAX_COUNT)) {
		ispv1_set_aecagc_mode(MANUAL_AECAGC);
		print_info("AEC/AGC OK, set to manual AECAGC, frame_count %d########", frame_count);

		if (get_awbtest_policy() == FLASH_AWBTEST_POLICY_FIXED) {
			ispv1_set_awb_mode(MANUAL_AWB);
			print_info("AWB set to manual, need not test flash awb########");
		}
		ispdata->flash_flow = FLASH_FROZEN;
	} else if (ispdata->flash_flow == FLASH_FROZEN) {
		/* stage 3: FLASH_FROZEN */
		frozen_frame++;

		if (get_awbtest_policy() == FLASH_AWBTEST_POLICY_FIXED) {
			/* frozen second frame, get locked AE value, maybe need adjust lum_max */
			if (frozen_frame > 1) {
				preflash_ae->gain = get_writeback_gain();
				preflash_ae->expo = get_writeback_expo();
				preflash_ae->lum = cur_lum;
				preflash_ae->lum_max = cur_lum; /* set default value */
				preflash_ae->lum_sum = cur_lum; /* set default value */

				/* if Low light, get preflash lum_max */
				if (preview_ae->lum < target_y_low) {
					unit_area = ispv1_get_stat_unit_area();
					ispv1_get_raw_lum_info(preflash_ae, unit_area);
				}
				goto normal_fixawb_out;
			} else {
				/* do nothing, just wait and return */
				return;
			}
		} else {
			/* check whether awb is stable. */
			if (frozen_frame == 1) {
				/* first frame, set last_awb init value as cur_awb */
				memcpy(&last_awb, &cur_awb, sizeof(awb_gain_t));
				return;
			}

			/* odd frames, we compare cur_awb with last_awb, if differ too much, continue. */
			if (frozen_frame % 2 != 0 && frozen_frame < FLASH_TEST_MAX_COUNT) {
				diff_awb.b_gain = abs((int)cur_awb.b_gain - (int)last_awb.b_gain);
				diff_awb.r_gain = abs((int)cur_awb.r_gain - (int)last_awb.r_gain);

				if (diff_awb.b_gain < 0x3 && diff_awb.r_gain < 0x3) {
					frozen_frame = FLASH_TEST_MAX_COUNT;
				} else {
					/* update last_awb using cur_awb. */
					memcpy(&last_awb, &cur_awb, sizeof(awb_gain_t));
					return;
				}
			}

			if (frozen_frame < FLASH_TEST_MAX_COUNT) {
				return;
			} else if (frozen_frame == FLASH_TEST_MAX_COUNT) {
				/* awb test OK or frozen_frame count reach MAX_COUNT*/
				ispv1_set_awb_mode(MANUAL_AWB);
				print_info("AWB differ OK or Reach MAX_COUNT, set to manual AWB########");
				return;
			} else {
				/* get locked AE value, maybe need adjust lum_max */
				preflash_ae->gain = get_writeback_gain();
				preflash_ae->expo = get_writeback_expo();
				preflash_ae->lum = cur_lum;
				preflash_ae->lum_max = cur_lum; /* set default value */
				preflash_ae->lum_sum = cur_lum; /* set default value */

				/* if Low light, get preflash lum_max */
				if (preview_ae->lum < target_y_low) {
					unit_area = ispv1_get_stat_unit_area();
					ispv1_get_raw_lum_info(preflash_ae, unit_area);
				}

				/* calculate capture flash lum */
				ispv1_cal_ratio_lum(preview_ae, preflash_ae, &capflash_ae, &preview_ratio_lum, &weight);

				/* calculate flash awb */
				ispv1_cal_flash_awb(weight.preflash_env, weight.preflash_flash, preview_awb, &cur_awb, &flash_awb);

				match_result = ispv1_check_awb_match(&flash_awb, led_awb0, led_awb1, weight.capflash_env);
				need_flash = ae_is_need_flash(sensor, preview_ae, target_y_low);
				if (preview_awb->b_gain >= FLASH_PREVIEW_LOWCT_TH)
					low_ct = true;
				else
					low_ct = false;

				if (low_ct == true && need_flash == false) {
					memcpy(&flash_awb, preset_awb, sizeof(awb_gain_t));
				} else if (match_result == true) {
					print_info("awb freego match, before compesate awb[B 0x%x, R 0x%x]", flash_awb.b_gain, flash_awb.r_gain);
					flash_awb.b_gain -= (flash_awb.b_gain / 16);
					flash_awb.r_gain += (flash_awb.r_gain / 16);
				} else {
					memcpy(&flash_awb, preset_awb, sizeof(awb_gain_t));
				}

				goto awbtest_out;
			}
		}

normal_fixawb_out:
		ispv1_cal_ratio_lum(preview_ae, preflash_ae, &capflash_ae, &preview_ratio_lum, &weight);
		memcpy(&flash_awb, preset_awb, sizeof(awb_gain_t));

awbtest_out:
		ispv1_cal_capture_awb(preview_awb, &flash_awb, &capture_awb,
			weight.capflash_env, weight.capflash_flash);

		/* To Capture_cmd: update preview_ratio_lum and flash ratio factor */
		isp_hw_data.preview_ratio_lum = preview_ratio_lum;

		/* configure ratio_factor */
		isp_hw_data.ratio_factor = ispv1_cal_ratio_factor(preview_ae, preflash_ae, &capflash_ae, target_y_low);

		print_info("preview[gain:0x%x,expo:0x%x,lum:0x%x,lumMax:0x%x,lumSum:0x%x], awb[B 0x%x, R 0x%x], preflash_weight_env 0x%x",
			preview_ae->gain, preview_ae->expo, preview_ae->lum, preview_ae->lum_max, preview_ae->lum_sum,
			preview_awb->b_gain, preview_awb->r_gain, weight.preflash_env);
		print_info("preflash[gain:0x%x,expo:0x%x,lum:0x%x,lumMax:0x%x,lumSum:0x%x], awb[B 0x%x, R 0x%x], preview_ratio_lum 0x%x",
			preflash_ae->gain, preflash_ae->expo, preflash_ae->lum, preflash_ae->lum_max, preflash_ae->lum_sum,
			cur_awb.b_gain, cur_awb.r_gain, preview_ratio_lum);
		print_info("capflash y:0x%x, max:0x%x, ratio_factor:0x%x, awb[0x%x:0x%x], capflash weight 0x%x",
			capflash_ae.lum, capflash_ae.lum_max, isp_hw_data.ratio_factor,
			capture_awb.b_gain, capture_awb.r_gain, weight.capflash_flash);

		ispv1_set_wb_value(&capture_awb);
		ispdata->flash_flow = FLASH_DONE;
		ispv1_check_flash_exit();
		frozen_frame = 0;
		frame_count = 0;
	}
}

void ispv1_dynamic_ydenoise(camera_sensor *sensor, bool flash_on)
{
	int index = sensor->preview_frmsize_index;
	u32 ae_th[3];
	u32 ae_value = get_writeback_gain() * get_writeback_expo() / 0x10;
	u8 ydenoise_value[5] = {
		ISP_YDENOISE_COFF_1X, /* gain 1x y denoise */
		ISP_YDENOISE_COFF_1X, /* gain 2x y denoise */
		ISP_YDENOISE_COFF_4X, /* gain 4x y denoise */
		ISP_YDENOISE_COFF_8X, /* gain 8x y denoise */
		ISP_YDENOISE_COFF_16X}; /* gain 16x y denoise */

	if (flash_on == false) {
		ae_th[0] = sensor->frmsize_list[index].banding_step_50hz * 0x18; /* (1band expo and 0x18 gain) */
		ae_th[1] = 3 * sensor->frmsize_list[index].banding_step_50hz * 0x10; /* (3band expo and 0x10 gain) */
		ae_th[2] = sensor->frmsize_list[index].vts * 0x20; /* (vts expo and 0x20 gain) */

		if (sensor->frmsize_list[index].binning == false) {
			ae_th[0] *= 2;
			ae_th[1] *= 2;
			ae_th[2] *= 2;
		}

		/* simplify dynamic denoise threshold*/
		if (ae_value <= ae_th[0]) {
			ydenoise_value[0] = ISP_YDENOISE_COFF_1X;
			ydenoise_value[1] = ISP_YDENOISE_COFF_1X;
		} else {
			ydenoise_value[0] = ISP_YDENOISE_COFF_2X;
			ydenoise_value[1] = ISP_YDENOISE_COFF_2X;
		}
	} else {
		/* should calculated in capture_cmd again. */
		ydenoise_value[0] = 0;
		ydenoise_value[1] = 0;
	}

	SETREG8(REG_ISP_YDENOISE_1X, ydenoise_value[0]);
	SETREG8(REG_ISP_YDENOISE_2X, ydenoise_value[1]);
	SETREG8(REG_ISP_YDENOISE_4X, ydenoise_value[2]);
	SETREG8(REG_ISP_YDENOISE_8X, ydenoise_value[3]);
	SETREG8(REG_ISP_YDENOISE_16X, ydenoise_value[4]);
}

void ispv1_dynamic_framerate(camera_sensor *sensor, camera_iso iso)
{
	static u32 count;

	u16 gain_th_high = AUTO_FRAME_RATE_MAX_GAIN;
	u16 gain_th_low = AUTO_FRAME_RATE_MIN_GAIN;
	u16 gain;
	int ret;
	int index;

	camera_frame_rate_dir direction;
	camera_frame_rate_state state = ispv1_get_frame_rate_state();

	if (CAMERA_ISO_AUTO != iso) {
		if (state == CAMERA_EXPO_PRE_REDUCE || ispv1_get_frame_rate_level() != 0) {
			ispv1_change_frame_rate(&state, CAMERA_FRAME_RATE_UP, sensor);
			ispv1_set_frame_rate_state(state);
		}
	} else {
		if (state == CAMERA_EXPO_PRE_REDUCE) {
			ret = ispv1_change_frame_rate(&state, CAMERA_FRAME_RATE_UP, sensor);
			ispv1_set_frame_rate_state(state);
			return;
		}

		/*get gain from isp */
		gain = get_writeback_gain();

		/* added for non-binning frame size auto frame rate control start */
		index = sensor->preview_frmsize_index;

		if (sensor->frmsize_list[index].binning == false) {
			gain_th_high *= 2;
			gain_th_low *= 2;
		}

		if (gain > gain_th_high) {
			direction = CAMERA_FRAME_RATE_DOWN;
			count++;
		} else if (gain < gain_th_low) {
			direction = CAMERA_FRAME_RATE_UP;
			count++;
		} else {
			count = 0;
		}
		/* added for non-binning frame size auto frame rate control end */

		if (count >= AUTO_FRAME_RATE_TRIGER_COUNT) {
			if (GETREG8(REG_ISP_AECAGC_STABLE)) {
				ret = ispv1_change_frame_rate(&state, direction, sensor);
				ispv1_set_frame_rate_state(state);
				count = 0;
			}
		}
	}
}

void ispv1_preview_done_do_tune(void)
{
	k3_isp_data *ispdata = this_ispdata;
	camera_sensor *sensor;
	static k3_last_state last_state = {CAMERA_SATURATION_MAX, CAMERA_CONTRAST_MAX, CAMERA_BRIGHTNESS_MAX, CAMERA_EFFECT_MAX};
	u8 target_y_low = GETREG8(REG_ISP_TARGET_Y_LOW);
	u32 unit_area;

	camera_frame_rate_state state = ispv1_get_frame_rate_state();

	if (NULL == ispdata) {
		return;
	}

	print_debug("preview_done, gain 0x%x, expo 0x%x, current_y 0x%x, flash_on %d",
		get_writeback_gain(), get_writeback_expo(), get_current_y(), this_ispdata->flash_on);

	if (false == ispdata->flash_on) {
		isp_hw_data.preview_ae.gain = get_writeback_gain();
		isp_hw_data.preview_ae.expo = get_writeback_expo();
		isp_hw_data.preview_ae.lum = get_current_y();
		/* set default value for lum_max and lum_sum */
		isp_hw_data.preview_ae.lum_max = isp_hw_data.preview_ae.lum;
		isp_hw_data.preview_ae.lum_sum = isp_hw_data.preview_ae.lum;

		if (false == afae_ctrl_is_null() && isp_hw_data.preview_ae.lum < target_y_low) {
			unit_area = ispv1_get_stat_unit_area();
			ispv1_get_raw_lum_info(&isp_hw_data.preview_ae, unit_area);
		}
		ispv1_get_wb_value(&isp_hw_data.preview_awb);
	}

	sensor = ispdata->sensor;
	if (CAMERA_USE_SENSORISP == sensor->isp_location) {
		print_debug("auto frame_rate only effect at k3 isp");
		return;
	}

	if ((FLASH_TESTING == ispdata->flash_flow) || (FLASH_FROZEN == ispdata->flash_flow)) {
		if (state == CAMERA_EXPO_PRE_REDUCE || ispv1_get_frame_rate_level() != 0) {
			ispv1_set_aecagc_mode(AUTO_AECAGC);
			ispv1_change_frame_rate(&state, CAMERA_FRAME_RATE_UP, sensor);
			ispv1_set_frame_rate_state(state);
			isp_hw_data.ae_resume = true;
		} else {
			ispv1_poll_flash_lum();
		}
	} else if (!afae_ctrl_is_null() && ((get_focus_state() == FOCUS_STATE_AF_PREPARING) ||
		(get_focus_state() == FOCUS_STATE_AF_RUNNING) ||
		(get_focus_state() == FOCUS_STATE_CAF_RUNNING))) {
		print_debug("focusing metering, should not change frame rate.");
	} else if (isp_hw_data.ae_resume == true && ispdata->flash_on == false) {
		ispv1_set_aecagc_mode(AUTO_AECAGC);
		ispv1_change_frame_rate(&state, CAMERA_FRAME_RATE_DOWN, sensor);
		isp_hw_data.ae_resume = false;
	} else {
		ispv1_dynamic_framerate(sensor, ispdata->iso);
	}

	if (true == camera_ajustments_flag) {
		last_state.saturation = CAMERA_SATURATION_MAX;
		last_state.contrast = CAMERA_CONTRAST_MAX;
		last_state.brightness = CAMERA_BRIGHTNESS_MAX;
		last_state.effect = CAMERA_EFFECT_MAX;
		camera_ajustments_flag = false;
	}

	/*camera_effect_saturation_done*/
	if ((ispdata->effect != last_state.effect)||(ispdata->saturation != last_state.saturation)) {
		ispv1_set_effect_saturation_done(ispdata->effect, ispdata->saturation);
		last_state.effect = ispdata->effect;
		last_state.saturation = ispdata->saturation;
	}

	/*contrast_done*/
	if (ispdata->contrast != last_state.contrast) {
		ispv1_set_contrast_done(ispdata->contrast);
		last_state.contrast = ispdata->contrast;
	}

	/*brightness_done*/
	if (ispdata->brightness != last_state.brightness) {
		ispv1_set_brightness_done(ispdata->brightness);
		last_state.brightness = ispdata->brightness;
	}

	if (sensor->awb_dynamic_ccm_gain != NULL) {
		u32 frame_index, ae_value, gain, exposure_line;
		awb_gain_t awb_gain;
		ccm_gain_t ccm_gain;

		GETREG16(REG_ISP_AWB_ORI_GAIN_B, awb_gain.b_gain);
		GETREG16(REG_ISP_AWB_ORI_GAIN_R, awb_gain.r_gain);

		gain= get_writeback_gain();
		exposure_line= get_writeback_expo() >> 4;
		ae_value = gain * exposure_line;

		frame_index = sensor->preview_frmsize_index;
		sensor->awb_dynamic_ccm_gain(frame_index, ae_value, &awb_gain, &ccm_gain);

		SETREG8(REG_ISP_CCM_PREGAIN_R, ccm_gain.r_gain);
		SETREG8(REG_ISP_CCM_PREGAIN_B, ccm_gain.b_gain);
	}

#ifndef OVISP_DEBUG_MODE
	ispv1_dynamic_ydenoise(sensor, ispdata->flash_on);
#endif

	ispv1_wakeup_focus_schedule(false);
}

/*
 * Used for tune ops and AF functions to get isp_data handler
 */
void ispv1_tune_ops_init(k3_isp_data *ispdata)
{
	this_ispdata = ispdata;

	if (ispdata->sensor->isp_location != CAMERA_USE_K3ISP)
		return;

	/* maybe some fixed configurations, such as focus, ccm, lensc... */
	if (ispdata->sensor->af_enable)
		ispv1_focus_init();
	ispv1_init_DPC(1, 1);

	ispv1_init_rawDNS(1);
	ispv1_init_uvDNS(1);
	ispv1_init_GbGrDNS(1);
	ispv1_init_RGBGamma(1);
	camera_ajustments_flag = true;

	ispv1_set_frame_rate_state(CAMERA_FRAME_RATE_HIGH);

	/*
	 *y36721 2012-02-08 delete them for performance tunning.
	 *ispv1_init_CCM(ispdata->sensor->image_setting.ccm_param);
	 *ispv1_init_LENC(ispdata->sensor->image_setting.lensc_param);
	 *ispv1_init_AWB(ispdata->sensor->image_setting.awb_param);
	 */

	ispv1_init_sensor_config(ispdata->sensor);

	ispv1_save_aecawb_step(&isp_hw_data.aecawb_step);
}

/*
 * something need to do after camera exit
 */
void ispv1_tune_ops_exit(void)
{
	if (this_ispdata->sensor->af_enable == 1)
		ispv1_focus_exit();
}

/*
 * something need to do before start_preview and start_capture
 */
void ispv1_tune_ops_prepare(camera_state state)
{
	k3_isp_data *ispdata = this_ispdata;
	camera_sensor *sensor = ispdata->sensor;

	if (STATE_PREVIEW == state) {

		/* For AF update */
		if (sensor->af_enable)
			ispv1_focus_prepare();

		if ((CAMERA_USE_SENSORISP == sensor->isp_location) &&
			(STATE_PREVIEW == state)) {
			ispv1_set_ae_statwin(&ispdata->pic_attr[state]);
		}

		/* need to check whether there is binning or not */
		ispv1_init_BC(1, 0, 0);

		ispdata->flash_flow = FLASH_DONE;
	} else if (STATE_CAPTURE == state) {
		/* we can add some other things to do before capture */
	}

	/* update lens correction scale size */
	ispv1_update_LENC_scale(ispdata->pic_attr[state].in_width, ispdata->pic_attr[state].in_height);
}

/*
 * something need to do before stop_preview and stop_capture
 */
void ispv1_tune_ops_withdraw(camera_state state)
{
	if (this_ispdata->sensor->af_enable)
		ispv1_focus_withdraw();
}
