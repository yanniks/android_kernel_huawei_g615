/* Copyright (c) 2008-2011, Hisilicon Tech. Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above
 *	   copyright notice, this list of conditions and the following
 *	   disclaimer in the documentation and/or other materials provided
 *	   with the distribution.
 *	 * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *	   contributors may be used to endorse or promote products derived
 *	   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef EDC_OVERLAY_H
#define EDC_OVERLAY_H

#include "k3_edc.h"


enum {
	OVERLAY_PIPE_EDC0_CH1,
	OVERLAY_PIPE_EDC0_CH2,
	OVERLAY_PIPE_EDC1_CH1,
	OVERLAY_PIPE_EDC1_CH2,
	OVERLAY_PIPE_MAX
};

#define OVERLAY_TYPE_CHCAP_ALL		0x01
#define OVERLAY_TYPE_CHCAP_PARTIAL	0x02

#define OVERLAY_PIPE_GRAPHIC		0x01
#define OVERLAY_PIPE_VIDEO			0x02

struct edc_capability {
	u32 alpha_enable:1;
	u32 ckey_enable:1;
	u32 filter_enable:1;
	u32 csc_enable:1;
	u32 reserved:28;
};

struct edc_channel_info {
	struct edc_capability cap;

	u8 alp_src;
	u8 alpha0;
	u8 alpha1;
	u32 ckeymin;
	u32 ckeymax;
};

/**
 * @struct
 * @brief
 */
struct edc_overlay_pipe {
	u32 pipe_type;
	u32 pipe_num;
	u32 edc_base;
	struct overlay_info req_info;
	struct edc_channel_info  edc_ch_info;

	void (*set_EDC_CHL_ADDR)(u32 edc_base, u32 nVal);
	void (*set_EDC_CHR_ADDR)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CTL_ch_en)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CTL_secu_line)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CTL_bgr)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CTL_pix_fmt)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CTL_colork_en)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_COLORK_MIN)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_COLORK_MAX)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_XY)(u32 edc_base, u32 nVal1, u32 nVal2);
	void (*set_EDC_CH_SIZE)(u32 edc_base, u32 nVal1, u32 nVal2);
	void (*set_EDC_CH_STRIDE)(u32 edc_base, u32 nVal);

	void (*set_EDC_CH_CSCIDC_csc_en)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCIDC)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCODC)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCP0)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCP1)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCP2)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCP3)(u32 edc_base, u32 nVal);
	void (*set_EDC_CH_CSCP4)(u32 edc_base, u32 nVal);
};

struct edc_overlay_ctrl {
	struct edc_overlay_pipe plist[OVERLAY_PIPE_MAX];
	/*u32 panel_mode;*/
};


int edc_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info, int id);
int edc_fb_suspend(struct fb_info *info);
int edc_fb_resume(struct fb_info *info);
int edc_fb_disable(struct fb_info *info);
int edc_fb_enable(struct fb_info *info);

void edc_overlay_init(struct edc_overlay_ctrl *ctrl);
int edc_overlay_get(struct fb_info *info, struct overlay_info *req);
int edc_overlay_set(struct fb_info *info, struct overlay_info *req);
int edc_overlay_unset(struct fb_info *info, int ndx);
int edc_overlay_play(struct fb_info *info, struct overlay_data *req);
int smartbl_ctrl_resume(struct k3_fb_data_type *k3fd);
int smartbl_ctrl_set(struct k3_fb_data_type *k3fd);
int set_sbl_bkl(struct k3_fb_data_type *k3fd, u32 value);

#endif  /* EDC_OVERLAY_H  */
