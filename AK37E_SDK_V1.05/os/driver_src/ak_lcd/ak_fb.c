/*
 * AK37D fb driver
 *
 * Copyright (C) 2018 Anyka(Guangzhou) Microelectronics Technology Co., Ltd.
 *
 * Author: Feilong Dong <dong_feilong@anyka.com>
 *		   Zhipeng Zhang <zhang_zhipeng@anyka.com>
 * 		   Guohong  Ye  <ye_guohong@anyka.com>
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <asm/fb.h>
#include <linux/vmalloc.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <mach/map.h>

#include "ak_fb.h"
#include "ak_lcdc.h"
// #include "ak_boot_logo.h" /* bootup logo display */

/*
 * lcd_ctl_force_init:
 * 0- if boot code had initial lcd device than donot initial again at here.
 * others- force initial lcd device
 */
static unsigned long lcd_ctl_force_init = 0x0;

module_param(lcd_ctl_force_init, ulong, S_IRUGO);

/*
 * lcdc_wait_mode
 * 0 - the orginal design,  when accept the ioctl set cmd, if fb_dev->par_change is true, will msleep 15ms
 * 1 - while user invoke ioctl to set fb var param, will return immediately , without any delay
 * others - won't return until the new setting take effect or timeout happen
 */
static unsigned long lcdc_wait_mode = 0x0;
module_param(lcdc_wait_mode, ulong, S_IRUGO);

/*
 * busy_bh
 * 0 - the par_change flag will be set before doing the lcdc param reg setting.
 * others - the par_change flag will be set after doing the lcdc param reg setting
 */
static unsigned long busy_bh = 0x0;
module_param(busy_bh, ulong, S_IRUGO);

/*
 * fb_shadow:
 * 0: do not create shadow fb
 * others: create shadow fb
 */
static int fb_shadow = 0;
module_param(fb_shadow, int, S_IRUGO);

/*
 * fb_shadow_dmasize:
 */
static int fb_shadow_dmasize = 0;
module_param(fb_shadow_dmasize, int, S_IRUGO);

static char panel_compatible[128];
module_param_string(panel_compatible, panel_compatible, sizeof(panel_compatible), S_IRUGO); // 声明参数

extern int get_partition_data(char *partition_name, char *data, unsigned long *date_len);

/* for load logo from mtd part */
extern void *partition_open(unsigned char *name);
extern unsigned long partition_get_data_size(void *handle);
extern int partition_read(void *handle, unsigned char *data, unsigned long data_len);
extern int partition_close(void *handle);

static int mpu_write_reg(int ctl, unsigned int data, struct ak_fb *fb_dev);
static void mipi_dsi_cfg(struct ak_fb *fb_dev);
static void set_dsi_dphy_output_clock(struct ak_fb *fb_dev);
static void ak_lcdc_rgb_display(struct ak_fb *fb_dev);
static void set_var_params(struct ak_fb *fb_dev);
static void ak_lcdc_set_irq_mask(struct ak_fb *fb_dev);

/*
 * ak_lcdc_pause -
 * set lcd controler stop
 *
 * @fb_dev:			pointor to fb
 */
static void ak_lcdc_pause(struct ak_fb *fb_dev)
{
	__raw_writel(0x1, fb_dev->lcdc_reg + AK_LCD_GO);
}

/*
 * ak_lcdc_dsi_sys_err_recover -
 * recover the sys error of dsi lcd
 *
 * @fb_dev:			pointor to fb
 */
static void ak_lcdc_dsi_sys_err_recover(struct ak_fb *fb_dev)
{
	unsigned int value;
	struct lcdc_dsi_info *dsi_info = fb_dev->dsi_info;

	/*LCD sys_pause*/
	ak_lcdc_pause(fb_dev);

	/*
	 * Close the LCD's IRQ(Keep same with RTOS)
	 */
	__raw_writel(0, fb_dev->lcdc_reg + AK_LCD_INT_MASK);

	/*080000fc b18 =1, gclk_clk_rst, reset mipi dsi controller
	 * 维持DSI APB 寄存器接口复位*/
#ifdef CONFIG_MACH_AK37E
	lcdc_set_reg(1, AK_CLK_SOFT_RST, 31, 31);
#else
	lcdc_set_reg(1, AK_CLK_GATE_AND_SOFT_RST, 18, 18);
#endif

	/*08000100 b30 = 1,lcd_pclk_rst_cfg, reset the module driven by lcd pclk
	 * 维持PCLK域复位状态*/
	lcdc_set_reg(1, AK_LCD_PCLK_CTRL, 30, 30);

	/*reset DSI controller txbyteclkhs
	 * 维持 BYTE CLK域复位状态*/
	lcdc_set_reg(1, AK_LCD_DSI_CLK_CFG, 29, 29);

	/*reset DSI controller & dphy txclkesc
	 * 维持 ESCAPE CLK域复位状态*/
	lcdc_set_reg(1, AK_LCD_DSI_CLK_CFG, 28, 28);

	/*reset dsi dphy
	 * 维持 mipi LCD DPHY复位状态*/
	lcdc_set_reg(0, AK_LCD_DSI_CLK_CFG, 30, 30);

	/*080000fc b18 =0, gclk_clk_rst, release reset mipi dsi controller
	 * release维持DSI APB 寄存器接口复位*/
#ifdef CONFIG_MACH_AK37E
	lcdc_set_reg(0, AK_CLK_SOFT_RST, 31, 31);
#else
	lcdc_set_reg(0, AK_CLK_GATE_AND_SOFT_RST, 18, 18);
#endif

	/*release reset DSI controller txbyteclkhs
	 * release 维持 BYTE CLK域复位状态*/
	lcdc_set_reg(0, AK_LCD_DSI_CLK_CFG, 29, 29);

	/*release reset DSI controller & dphy txclkesc
	 * release 维持 ESCAPE CLK域复位状态*/
	lcdc_set_reg(0, AK_LCD_DSI_CLK_CFG, 28, 28);

	/* lane pad swap setting */
	value = (((dsi_info->txd0) | (dsi_info->txd1 << 3) | (dsi_info->txd2 << 6) | (dsi_info->txd3 << 9) | (dsi_info->txd4 << 12)) << 17);
	__raw_writel(value, AK_LCD_DSI_LANE_SWAP);

	// lcdc_set_reg(0, fb_dev->lcdc_reg + AK_DPI_CONFIG_REG, 8, 0);

	mipi_dsi_cfg(fb_dev);

	set_dsi_dphy_output_clock(fb_dev);

	/*release reset dsi dphy*/
	/*释放mipi LCD DPHY复位*/
	lcdc_set_reg(1, AK_LCD_DSI_CLK_CFG, 30, 30);

	/* check if the DPHY is ready */
	/*等待DPHY READY*/
	do
	{
	} while (!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & (1 << 2)));

	// set_var_params(fb_dev); // dma address may be changed if lcd resume

	/*08000100 b30 = 0,lcd_pclk_rst_cfg, release reset the module driven by lcd pclk
	 * release维持PCLK域复位状态*/
	lcdc_set_reg(0, AK_LCD_PCLK_CTRL, 30, 30);

	__raw_writel(0x1 << 2, fb_dev->lcdc_reg + AK_LCD_GO);

	/*
	 * Open the LCD's IRQ(Keep same with RTOS)
	 */
	ak_lcdc_set_irq_mask(fb_dev);
}

/*
 * ak_lcdc_sys_err_recover -
 * recover the sys error of all type lcd
 *
 * @fb_dev:			pointor to fb
 */
static void ak_fb_sys_err_recover(struct ak_fb *fb_dev)
{
	switch (fb_dev->panel_info->rgb_if)
	{
	case DISP_IF_RGB:
		/*case RGB*/
		ak_lcdc_pause(fb_dev);
		__raw_writel(0x1 << 2, fb_dev->lcdc_reg + AK_LCD_GO);
		break;

	case DISP_IF_DSI:
		/*case DSI*/
		ak_lcdc_dsi_sys_err_recover(fb_dev);
		break;

	case DISP_IF_MPU:
		/* set system stop signal to stop reflash */
		ak_lcdc_pause(fb_dev);
		/* re-set mpu display go */
		__raw_writel(0x1 << 3, fb_dev->lcdc_reg + AK_LCD_GO);
		break;

	default:
		pr_err("%s lcd if:%d not support\n", __func__, fb_dev->panel_info->rgb_if);
		break;
	}
}

/*
 * ak_fb_go_enable -
 * start lcd controller up working
 *
 * @fb_dev:			pointor to fb
 *
 */
static void ak_fb_go_enable(struct ak_fb *fb_dev)
{
	u32 regval;

	switch (fb_dev->panel_info->rgb_if)
	{
	case DISP_IF_RGB:
	case DISP_IF_DSI:
		/*case RGB & DSI*/
		// ak_lcdc_pause(fb_dev);
		__raw_writel(0x1 << 2, fb_dev->lcdc_reg + AK_LCD_GO);
		break;

	case DISP_IF_MPU:
		/*send writting MPU GRAM command: 0x2c*/
		mpu_write_reg(LCD_MPU_CMD, MPU_WRITE_DATA_CMD, fb_dev);
		__raw_writel(0x1 << 3, fb_dev->lcdc_reg + AK_LCD_GO);

		while (1)
		{
			regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_INT_STATUS);
			if ((regval & (1 << 18)) == (1 << 18))
				pr_info("fifo alarm!\n");

			if ((regval & (1 << 17)) == (1 << 17))
				pr_info("alert valid!\n");

			if ((regval & (1 << 2)) == (1 << 2))
			{
				pr_info("display ok!\n");
				break;
			}
		}
		break;

	default:
		pr_err("%s lcd if:%d not support\n", __func__, fb_dev->panel_info->rgb_if);
		break;
	}
}

/*
 * set_var_params-
 * update var infomations
 *
 * @fb_dev:			pointor to fb
 */
static void set_var_params(struct ak_fb *fb_dev)
{
	u32 regval;

	/* input data format is determined by bit[8] and bit[13] of this register.
	 * 00:input data format is 16bits (RGB565 or BGR565)
	 * 01:input data format is 24bits (RGB888 or BGR888)
	 * 10 or 11:input data format is 32bits (ARGB888 ABGR888 RGBA888 BGRA888)
	 * bit[12] input data seq, 0:RGB,1:BGR
	 * bit[7] inout a location, 0: A front, 1, A back
	 */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	regval &= ~((0x3 << 7) | (0x3 << 12));
	regval |= ((fb_dev->rgb_input->a_location << 7) | (fb_dev->rgb_input->fmt1 << 8) | (fb_dev->rgb_input->rgb_seq << 12) | (fb_dev->rgb_input->fmt0 << 13));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

	/* config RGB input size */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_SIZE);
	regval &= ~(0x3FFFFF);
	regval |= ((fb_dev->rgb_input->height) | (fb_dev->rgb_input->width << 11));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_SIZE);

	/* config RGB input offset */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_OFFSET);
	regval &= ~(0x3FFFFF);
	regval |= ((fb_dev->rgb_input->v_offset) | (fb_dev->rgb_input->h_offset << 11));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_OFFSET);

	/* config RGB dma address */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_RGB_CTRL);
	regval &= ~(0x3FFFFFFF);
	regval |= ((fb_dev->panel_info->paddr + fb_dev->panel_info->paddr_offset) & 0x3FFFFFFF);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_RGB_CTRL);

	// pr_err("panel_info->width=%d\n",fb_dev->panel_info->width);
	// pr_err("panel_info->height=%d\n",fb_dev->panel_info->height);
	// pr_err("rgb_info->width=%d\n",fb_dev->rgb_input->width);
	// pr_err("rgb_info->height=%d\n",fb_dev->rgb_input->height);
	// pr_err("rgb_info->v_offset=%d\n",fb_dev->rgb_input->v_offset);
	// pr_err("rgb_info->h_offset=%d\n",fb_dev->rgb_input->h_offset);
	// pr_err("panel_info->paddr=0x%x, \n",fb_dev->panel_info->paddr);
	// pr_err("panel_info->paddr_offset=0x%x, \n",fb_dev->panel_info->paddr_offset);

	/* enable rgb channel */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	regval |= (0x1 << 3);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

	/* set sw ctrl en */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_SW_CTRL);
	regval |= (0x1 << 12);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_SW_CTRL);
}

#define RETRY_MAX 4
#define MSLEEP_TIME 4
/*
 * ak_lcdc_rgb_display -
 * update var informations and start lcd
 *
 * @fb_dev:			pointor to fb
 */
static void ak_lcdc_rgb_display(struct ak_fb *fb_dev)
{

	/* if busy_bh is zero, set the par_change flag before set param reg */
	if (busy_bh == 0)
	{
		/*set the par_change flag , and it will be reset in done irq handle */
		if ((fb_dev->panel_info->rgb_if != DISP_IF_MPU) && fb_dev->lcd_go)
		{
			fb_dev->par_change = true;
		}
	}

	set_var_params(fb_dev);

	if (busy_bh != 0)
	{
		/*set the par_change flag , and it will be reset in done irq handle */
		if ((fb_dev->panel_info->rgb_if != DISP_IF_MPU) && fb_dev->lcd_go)
		{
			fb_dev->par_change = true;
		}
	}

	/* mpu or rgb go*/
	if (fb_dev->panel_info->rgb_if == DISP_IF_MPU || fb_dev->lcd_go == false)
	{
		ak_fb_go_enable(fb_dev);
		fb_dev->lcd_go = true;
	}
	else
		;
}

/******************************************
 * FRAMEBUFFER SUPPORT
 ******************************************/

/*
 * convert_bitfield -
 * really component to bitfield translate
 *
 * @val:			component
 * @bf:				bitfiled of the component
 */
static inline u32 convert_bitfield(int val, struct fb_bitfield *bf)
{
	unsigned int mask = (1 << bf->length) - 1;

	return (val >> (16 - bf->length) & mask) << bf->offset;
}

/*
 * ak_fb_setcolreg -
 * set the pseudo_palette entry of fb_info
 *
 * @regno:		the entry index of the pseudo_palette
 * @red:		the value of red component
 * @green:		the value of green component
 * @blue:		the value of blue component
 * @transp:		the value of alpha component
 * @fb:			the framebuffer information of ak37c lcd driver
 */
static int ak_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
						   unsigned blue, unsigned transp,
						   struct fb_info *fb)
{
	/* anyka lcd controller only support true color */
	if (regno >= 16)
		return -EINVAL;

	((u32 *)(fb->pseudo_palette))[regno] = convert_bitfield(transp, &(fb->var.transp)) |
										   convert_bitfield(blue, &(fb->var.blue)) |
										   convert_bitfield(green, &(fb->var.green)) |
										   convert_bitfield(red, &(fb->var.red));

	return 0;
}

/**
 * @brief     Get the color format setting from fb_var_screeninfo struct
 * @author    Feilong Dong
 * @date      2019-04-16
 * @param     [in]  var :  fb_var_screeninfo struct of the ak37c lcd driver
 * @return    int :  if successful return the color format, otherwise return nagative
 * @retval    AKLCD_RGB565 :  the color format setting of @var is RGB565
 * @retval    AKLCD_BGR565 :  the color format setting of @var is BGR565
 * @retval    AKLCD_RGB888 :  the color format setting of @var is RGB888
 * @retval    AKLCD_BGR888 :  the color format setting of @var is BGR888
 * @retval    -EINVAL :  the parameters in @var is invalid
 */
static int __ak_fb_get_pixel_format(const struct fb_var_screeninfo *var)
{
	if (var->bits_per_pixel == 16)
	{
		if (var->red.offset == 11 && var->blue.offset == 0)
			return FMT_RGB565;

		if (var->red.offset == 0 && var->blue.offset == 11)
			return FMT_BGR565;
	}

	if (var->bits_per_pixel == 24)
	{
		if (var->red.offset == 16 && var->blue.offset == 0)
			return FMT_RGB888;

		if (var->red.offset == 0 && var->blue.offset == 16)
			return FMT_BGR888;
	}

	return -EINVAL;
}

/*
 * ak_fb_set_bitfields -
 * set bitfields of the comconents
 *
 * @var:			pointor to var info
 * @rgb_seq:		sequence of R/G/B
 */
static int ak_fb_set_bitfields(struct fb_var_screeninfo *var, int rgb_seq)
{
	int ret = 0;

	memset(&var->transp, 0, sizeof(var->transp));

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;

	switch (var->bits_per_pixel)
	{
	case 16:
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;
	case 24:
		var->red.length = 8;
		var->blue.length = 8;
		var->green.length = 8;
		break;
	case 32: // add it if need
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.length = 8;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/*
	 * >= 16bpp displays have separate colour component bitfields
	 * encoded in the pixel data.  Calculate their position from
	 * the bitfield length defined above.
	 */
	if (ret == 0)
	{
		if (rgb_seq == 1)
		{ // 1: RGB, 0:BGR
			var->blue.offset = 0;
			var->green.offset = var->blue.offset + var->blue.length;
			var->red.offset = var->green.offset + var->green.length;
		}
		else if (rgb_seq == 0)
		{
			var->red.offset = 0;
			var->green.offset = var->red.offset + var->red.length;
			var->blue.offset = var->green.offset + var->green.length;
		}
	}

	return ret;
}

/**
 * @brief     Check the new fb_var_screeninfo is valid or not
 * @author    Feilong Dong
 * @date      2019-04-16
 * @param     [in]  var :  new fb_var_screeninfo struct
 * @parm      [in]  fb :  fb_info of the ak37c lcd driver
 * @return    int :  if the new fb_var_screeninfo is valid return 0, otherwise return nagative
 * @retval    0 :  the new fb_var_screeninfo is valid
 * @retval    -EINVAL :  the new fb_var_screeninfo is invalid
 */
static int ak_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fb)
{
	struct ak_fb *ak_fb;
	struct device *dev;

	ak_fb = fb->par;
	dev = &(ak_fb->pdev->dev);

	/* virtual resolution fixed to panel size, can't be set by user */
	if (var->yres_virtual != ak_fb->panel_info->height)
		var->yres_virtual = ak_fb->panel_info->height;

	if (var->xres_virtual != ak_fb->panel_info->width)
		var->xres_virtual = ak_fb->panel_info->width;

	if (var->xres_virtual < var->xres || var->yres_virtual < var->yres)
	{
		return -EINVAL;
	}

	if (var->xoffset > (var->xres_virtual - var->xres) || var->yoffset > (var->yres_virtual - var->yres))
	{
		return -EINVAL;
	}

	if (var->xres > ak_fb->panel_info->width || var->yres > ak_fb->panel_info->height)
	{
		return -EINVAL;
	}

	if (__ak_fb_get_pixel_format(var) < 0)
	{
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief     Check the new fb_var_screeninfo is valid or not
 * @author    Feilong Dong
 * @date      2019-04-16
 * @param     [in]  var :  new fb_var_screeninfo struct
 * @parm      [in]  fb :  fb_info of the ak37c lcd driver
 * @return    int :  if the new fb_var_screeninfo is valid return 0, otherwise return nagative
 * @retval    0 :  the new fb_var_screeninfo is valid
 * @retval    -EINVAL :  the new fb_var_screeninfo is invalid
 */
static int ak_fb_shadow_check_var(struct fb_var_screeninfo *var, struct fb_info *fb)
{
	return 0;
}

/**
 * @brief     Set the new fb_var_screeninfo to device
 *            Note: only the fb_var_screeninfo can be changed by application
 * @author    Feilong Dong
 * @date      2019-04-16
 * @parm      [in]  fb :  fb_info of the ak37c lcd driver
 * @return    int :  if setting is successful return 0, otherwise return nagative
 * @retval    0 :  setting successful
 * @retval    -ENOMEM :  can't reallocate framebuffer memory
 */
static int ak_fb_set_par(struct fb_info *fb)
{
	struct ak_fb *fb_dev = fb->par;

	/* rgb input data size */
	fb_dev->rgb_input->width = fb->var.xres;
	fb_dev->rgb_input->height = fb->var.yres;
	fb_dev->rgb_input->h_offset = fb->var.xoffset;
	fb_dev->rgb_input->v_offset = fb->var.yoffset;
	fb_dev->rgb_input->rgb_seq = __ak_fb_get_pixel_format(&fb->var) & 0x1;
	fb_dev->rgb_input->fmt0 = (__ak_fb_get_pixel_format(&fb->var) & 0x2) >> 1;
	fb_dev->rgb_input->fmt1 = (__ak_fb_get_pixel_format(&fb->var) & 0x4) >> 2;
	// fb_dev->rgb_input->a_location = ; //for 32bits input data,not support now

	if (fb->var.bits_per_pixel / 8 != fb_dev->rgb_input->bpp)
	{
		pr_err("bits_per_pixel must be %d!", fb_dev->rgb_input->bpp * 8);
		return -EINVAL;
	}

	/* for double buffer add buffer change here */
	if (fb_dev->fb_type > 0)
	{
		fb_dev->fb_id = fb->var.reserved[0];
		printk("%s ========fb_id:%d\n", __func__, fb_dev->fb_id);
		fb_dev->panel_info->paddr_offset = fb_dev->panel_info->size * fb_dev->fb_id;
	}

	return 0;
}

/**
 * @brief     Set the new fb_var_screeninfo to device
 *            Note: only the fb_var_screeninfo can be changed by application
 * @author    Feilong Dong
 * @date      2019-04-16
 * @parm      [in]  fb :  fb_info of the ak37c lcd driver
 * @return    int :  if setting is successful return 0, otherwise return nagative
 * @retval    0 :  setting successful
 * @retval    -ENOMEM :  can't reallocate framebuffer memory
 */
static int ak_fb_shadow_set_par(struct fb_info *fb)
{
	return 0;
}

/*
 *  ak_fb_pan_display -
 *  Set the display region's origin on the virtual screen
 *
 *  @var:			pointor to var info
 *  @fb:			pointor to fb
 */
static int ak_fb_pan_display(struct fb_var_screeninfo *var,
							 struct fb_info *fb)
{
	int ret;
	struct ak_fb *fb_dev = fb->par;

	fb->var.xoffset = var->xoffset;
	fb->var.yoffset = var->yoffset;
	ret = ak_fb_set_par(fb);
	if (ret != 0)
		pr_err("ak_fb_pan_display(%d,%d) returns=%d\n", var->xoffset, var->yoffset, ret);

	/* check if the lcd controller is finished the last pan display */
	if (fb_dev->par_change == true)
	{
		pr_err("the last fb config not set properly\n");

		if (lcdc_wait_mode == 0x0)
		{
			usleep_range(15 * 1000, 15 * 1000);
			// msleep(15);
		}
	}

#if 0
	if (fb_dev->lcd_go) {
		fb_dev->par_change = true;
	}
#endif

	ak_lcdc_rgb_display(fb_dev);

	/* if enable force wait, will wait untile par_change turn to false or timeout */
	if (lcdc_wait_mode != 0x0 && lcdc_wait_mode != 0x01)
	{
		unsigned int cycle = 0;

		/* will check whether the new param in HW reg take effect before it can return time out will be 15ms*/
		while (cycle < RETRY_MAX)
		{
			if (fb_dev->par_change == false)
				break;
			/* sleep 4ms */
			usleep_range(MSLEEP_TIME * 1000, MSLEEP_TIME * 1000);
			cycle++;
		}
	}

	return 0;
}

/*
 *  ak_fb_shadow_pan_display -
 *  Set the display region's origin on the virtual screen
 *
 *  @var:			pointor to var info
 *  @fb:			pointor to fb
 */
static int ak_fb_shadow_pan_display(struct fb_var_screeninfo *var,
									struct fb_info *fb)
{
	return 0;
}

/*
 * ak_fb_blank -
 * blank display
 *
 * @blank:			blank flags
 * @fb:				pointor to fb
 */
static int ak_fb_blank(int blank, struct fb_info *fb)
{
	struct ak_fb *fb_dev = fb->par;

	switch (blank)
	{
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		/* disable rgb channel, set bg_color */
		if (fb_dev->lcd_go == true)
		{
			ak_lcdc_pause(fb_dev);
			fb_dev->lcd_go = false;
		}
		break;
	case FB_BLANK_UNBLANK:
		/* enable rgb channel, set rgb go */
		if (fb_dev->lcd_go == false)
		{
			ak_lcdc_rgb_display(fb_dev);
		}
		break;
	}

	return 0;
}

#define FBIOGET_CUS_LCDBUSY 0x4680

/*
 * ak_fb_ioctl -
 * ioctl
 *
 * @fb:			pointor to fb
 * @cmd:		command of control
 * @arg:		argument of command
 */
static int ak_fb_ioctl(struct fb_info *fb, unsigned int cmd, unsigned long arg)
{
	struct ak_fb *fb_dev = fb->par;
	void __user *argp = (void __user *)arg;
	long ret = 0;
	unsigned long var = 0;

	switch (cmd)
	{
	case FBIOGET_CUS_LCDBUSY:
		var = fb_dev->par_change;
		// pr_err("fb_dev->par_change is[%lu]\n",var);
		ret = copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
		break;
	default:
		ret = -ENOTTY;
	}
	return ret;
}

static ssize_t ak_fb_write(struct fb_info *fb, const char __user *buf, size_t count, loff_t *ppos)
{
	struct ak_fb *fb_dev = fb->par;
	unsigned long p = *ppos;
	u8 *buffer, *src;
	u8 __iomem *dst;
	int c, cnt = 0, err = 0;
	unsigned long total_size;

	// total_size = fb->screen_size;
	total_size = fb->fix.smem_len; // can write to double buffer

	if (p > total_size)
		return -EFBIG;

	if (count > total_size)
	{
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size)
	{
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
					 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u8 __iomem *)(fb->screen_base + p);

	while (count)
	{
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		if (copy_from_user(src, buf, c))
		{
			err = -EFAULT;
			break;
		}

		fb_memcpy_tofb(dst, src, c);
		dst += c;
		src += c;
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	if (fb_dev->panel_info->rgb_if == DISP_IF_MPU)
	{
		if ((*ppos == fb->screen_size) || (*ppos == fb->fix.smem_len))
		{
			ak_lcdc_rgb_display(fb_dev);
		}
		if ((*ppos) >= fb->fix.smem_len)
			*ppos = 0;
	}

	return (cnt) ? cnt : err;
}

/*fb ops*/
static struct fb_ops fb_dev_ops = {
	.owner = THIS_MODULE,
	/* needed by fb_set_cmap() */
	.fb_setcolreg = ak_fb_setcolreg,
	/* needed by FBIOPUT_VSCREENINFO */
	.fb_check_var = ak_fb_check_var,
	.fb_set_par = ak_fb_set_par,
	.fb_pan_display = ak_fb_pan_display,
	/* need by FBIOBLANK */
	.fb_blank = NULL,
	/* needed by fbcon */
	//.fb_fillrect	= cfb_fillrect,
	//.fb_copyarea	= cfb_copyarea,
	//.fb_imageblit	= cfb_imageblit,
	/* extended ioctl */
	.fb_ioctl = ak_fb_ioctl,
	.fb_write = ak_fb_write,
};

static struct fb_ops fb_shadow_dev_ops = {
	.owner = THIS_MODULE,
	/* needed by fb_set_cmap() */
	.fb_setcolreg = ak_fb_setcolreg,
	/* needed by FBIOPUT_VSCREENINFO */
	.fb_check_var = ak_fb_shadow_check_var,
	.fb_set_par = ak_fb_shadow_set_par,
	.fb_pan_display = ak_fb_shadow_pan_display,
	/* need by FBIOBLANK */
	.fb_blank = ak_fb_blank,
	/* needed by fbcon */
	//.fb_fillrect	= cfb_fillrect,
	//.fb_copyarea	= cfb_copyarea,
	//.fb_imageblit	= cfb_imageblit,
	/* extended ioctl */
	.fb_ioctl = ak_fb_ioctl,
	.fb_write = ak_fb_write,
};

/*
 * rgb_panel_info_cfg -
 * initialization of rgb lcd
 *
 * @fb_dev:			pointor to fb device
 */
static void rgb_panel_info_cfg(struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	struct lcdc_rgb_info *rgb_info = fb_dev->rgb_info;
	u32 regval;
	u32 thlen, tvlen;
	unsigned long flags;

	/* top register config and disable rgb channel */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	regval &= ~((0x1) | (0xF << 3) | (0x1 << 11) | (0x3FF << 14) | (0xFF << 24));
	regval |= (((!panel_info->pclk_pol) << 4) | (0x2 << 5) | ((!panel_info->rgb_seq) << 11) | (rgb_info->alarm_full << 14) | (rgb_info->alarm_empty << 24));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

	/* lcd rgb general info parameter config */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_RGB_GEN_INFO);
	regval = ((panel_info->vogate_pol) | (panel_info->vsync_pol << 1) | (panel_info->hsync_pol << 2) | (rgb_info->neg_blank_level << 3) | (rgb_info->pos_blank_level << 11) | (rgb_info->blank_sel << 19) | (rgb_info->dma_mode << 20) | (panel_info->bus_width << 21) | (rgb_info->tpg_mode << 23) | (rgb_info->rgb_even_seq << 24) | (rgb_info->rgb_odd_seq << 27) | (rgb_info->rgb_seq_sel << 30) | (rgb_info->dummy_seq << 31));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_RGB_GEN_INFO);

	/*disable vpage function*/
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_RGB_CTRL);
	regval &= ~(0x1 << 30);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_RGB_CTRL);

	/* set rgb bg color */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_BACKGROUND);
	regval &= ~(0xFFFFFF);
	regval |= (fb_dev->rgb_info->bg_color);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_BACKGROUND);

	/* rgb timing setting */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE1);
	regval &= ~(0xFFFFFF);
	regval |= ((panel_info->tvpw) | (panel_info->thpw << 12));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE1);

	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE2);
	regval &= ~(0xFFFFFF);
	regval |= ((panel_info->thd) | (panel_info->thb << 12));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE2);

	thlen = panel_info->thpw + panel_info->thb + panel_info->thd + panel_info->thf;
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE3);
	regval &= ~(0xFFFFFF);
	regval |= ((thlen) | (panel_info->thf << 13));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE3);

	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE4);
	regval &= ~(0xFFF);
	regval |= (panel_info->tvb);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE4);

	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE5);
	regval &= ~(0xFFF);
	regval |= (panel_info->tvf);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE5);

	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE6);
	regval &= ~((0x1FFF) | (0x1FFF << 14));
	regval |= ((rgb_info->v_unit) | (rgb_info->vh_delay_cycle << 1) | (rgb_info->vh_delay_en << 14) | (panel_info->tvd << 15));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE6);

	if (rgb_info->v_unit == 0)
	{
		tvlen = panel_info->tvpw + panel_info->tvb + panel_info->tvd + panel_info->tvf;
	}
	else
	{
		tvlen = panel_info->tvb + panel_info->tvd + panel_info->tvf;
	}
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_INTERFACE7);
	regval &= ~(0x1FFF);
	regval |= (tvlen);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_INTERFACE7);

	/* set panel size */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_PANEL_SIZE);
	regval &= ~(0x3FFFFF);
	regval |= ((panel_info->height) | (panel_info->width << 11));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_PANEL_SIZE);

	/* lcd sw ctrl settings */
	rgb_info->alert_line = panel_info->height - rgb_info->alert_line;
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_SW_CTRL);
	regval &= ~((0xFFF));
	regval |= ((rgb_info->alert_line) | (0x0 << 11));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_SW_CTRL);

	/* pclk domain reset, div setting and enable */
	local_irq_save(flags);
	lcdc_set_reg(1, AK_LCD_PCLK_CTRL, 30, 30);
	lcdc_set_reg(0, AK_LCD_PCLK_CTRL, 30, 30);
	local_irq_restore(flags);

	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_PCLK);
	regval &= ~(0x1FF);
	regval |= (0x1) | (panel_info->pclk_div << 1) | (0x1 << 8);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_PCLK);
}

/*
 * reg_panel_init -
 * initialization of rgb lcd by device node
 *
 * @child:				the device node
 * @fb_dev:				pointor to the fb device
 */
static void rgb_panel_init(struct device_node *child, struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;

	of_property_read_u32(child, "panel-rgb-seq", &panel_info->rgb_seq);
	of_property_read_u32(child, "panel-width", &panel_info->width);
	of_property_read_u32(child, "panel-height", &panel_info->height);
	of_property_read_u32(child, "panel-pclk-div", &panel_info->pclk_div);
	of_property_read_u32(child, "panel-thpw", &panel_info->thpw);
	of_property_read_u32(child, "panel-thb", &panel_info->thb);
	of_property_read_u32(child, "panel-thf", &panel_info->thf);
	of_property_read_u32(child, "panel-tvpw", &panel_info->tvpw);
	of_property_read_u32(child, "panel-tvb", &panel_info->tvb);
	of_property_read_u32(child, "panel-tvf", &panel_info->tvf);
	of_property_read_u32(child, "panel-pclk-pol", &panel_info->pclk_pol);
	of_property_read_u32(child, "panel-hsync-pol", &panel_info->hsync_pol);
	of_property_read_u32(child, "panel-vsync-pol", &panel_info->vsync_pol);
	of_property_read_u32(child, "panel-vogate-pol", &panel_info->vogate_pol);
	of_property_read_u32(child, "panel-bus-width", &panel_info->bus_width);

	panel_info->thd = panel_info->width;
	panel_info->tvd = panel_info->height;

	if (fb_dev->lcd_ctrl_inited)
	{
		printk(KERN_ERR "%s, line:%d\n", __func__, __LINE__);
		return;
	}
	rgb_panel_info_cfg(fb_dev);
}

/*
 * mipi_clk_and_pad_setting -
 * setting mipi clock & pins
 *
 * @fb_dev:			pointor to fb device
 */
static void mipi_clk_and_pad_setting(struct ak_fb *fb_dev)
{
	u32 regval;
	unsigned long flags;
	struct lcdc_dsi_info *dsi_info = fb_dev->dsi_info;

	/* lane pad swap setting */
	regval = (((dsi_info->txd0) | (dsi_info->txd1 << 3) | (dsi_info->txd2 << 6) | (dsi_info->txd3 << 9) | (dsi_info->txd4 << 12)) << 17);
	__raw_writel(regval, AK_LCD_DSI_LANE_SWAP);

	/* enable mipi dsi clk */
	clk_prepare_enable(fb_dev->dsi_clk);

	mdelay(10);

	local_irq_save(flags);
	lcdc_set_reg(1, AK_LCD_PCLK_CTRL, 30, 30);
	local_irq_restore(flags);

	/*DSI Controller & DPHY clock gate enable */
	regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
	regval &= ~(0x1 << 31);
	__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

	/* DSI DPHY Reset */
	regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
	regval &= ~(0x1 << 30);
	__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

	/* DSI Controller TxByteClkHS, DSI Controller & dphy TxClkEsc reset */
	regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
	regval |= (0x3 << 28);
	__raw_writel(regval, AK_LCD_DSI_CLK_CFG);
	mdelay(1);
	regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
	regval &= ~(0x3 << 28);
	__raw_writel(regval, AK_LCD_DSI_CLK_CFG);
}

/*
 * mipi_panel_cmd_setting -
 * configure mipi lcd
 *
 * @child:			pointor to device node
 * @fb_dev:			pointor to fb device
 */
static void mipi_panel_cmd_setting(struct device_node *child, struct ak_fb *fb_dev)
{
	struct property *panel_init;
	int num_inits;
	u32 delay, config;
	u32 payload_type;
	u32 short_data;
	unsigned char temp_i, temp, data_type, remainder, quotient;
	unsigned char long_packet[256];
	unsigned char short_packet[2];
	unsigned char *p;
	u32 regval;
	int i, j, err;

	panel_init = of_find_property(child, "panel-init-list", NULL);
	/*data unit is long int(4 bytes) in DTS, we need translating to char(1 bytes) here.   */
	num_inits = panel_init ? (panel_init->length / 4) : 0;

	/* check if the DPHY is ready */
	do
	{
	} while (!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & (1 << 2)));

	for (i = 0; i < num_inits;)
	{
		for (j = 0;; j++)
		{
			err = of_property_read_u32_index(child, "panel-init-list", i + j, &config);
			if (err)
				pr_err("panel-init-list get index:%d failed\n", i + j);

			if (j == 0)
				delay = config;
			else if (j == 1)
			{
				data_type = (unsigned char)config;
				/*** 0x03 Generic Short WRITE, no parameters Short
				***  0x13 Generic Short WRITE, 1 parameter Short
				***  0x23 Generic Short WRITE, 2 parameters Short
				***  0x05 DCS Short WRITE, no parameters Short
				***  0x15 DCS Short WRITE, 1 parameter Short
				***  0x29 Generic Long Write Long
				***  0x39 DCS Long Write/write_LUT Command Packet Long
				***/
				if (data_type == 0x03 || data_type == 0x13 || data_type == 0x23 || data_type == 0x05 || data_type == 0x15)
				{
					/* configure short packet cmd */
					payload_type = 1;
					regval = fb_dev->dsi_info->cmd_type | (0x1 << 1);
				}
				else if (data_type == 0x29 || data_type == 0x39)
				{
					/* configure long packet cmd  */
					payload_type = 0;
					regval = fb_dev->dsi_info->cmd_type | (0x0 << 1);
				}
				__raw_writel(regval, fb_dev->lcdc_reg + AK_DPI_CONFIG_REG);
			}
			else
			{
				/*zzp, 2020/5/27 translating to char(1 bytes) here. */
				if (config != 0xFFF)
				{
					if (payload_type == 1)
						short_packet[j - 2] = (unsigned char)config;

					if (payload_type == 0)
						long_packet[j - 2] = (unsigned char)config;
				}

				/*zzp, 2020/5/27 start to send packet. */
				if (config == 0xFFF)
				{
					/* sending short packet */
					if (payload_type == 1)
					{
						regval = (data_type | (fb_dev->dsi_info->vc << 6) | (short_packet[0] << 8) | (short_packet[1] << 16));
						__raw_writel(regval, fb_dev->lcdc_reg + AK_PACKET_HEAD_REG);
						/* check if the current packet tx done */
						do
						{
						} while (!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & 0x1));
						mdelay(delay);
					}

					/* sending long packet  */
					if (payload_type == 0)
					{
						p = long_packet;
						/* write long packet data to payload register. 4bytes every time. */
						for (quotient = 0; quotient < (j - 2) / 4; quotient++)
						{
							short_data = *(unsigned long *)p;
							p += 4;
							__raw_writel(short_data, fb_dev->lcdc_reg + AK_TX_PAYLOAD_REG);
						}

						remainder = (j - 2) % 4;
						if (remainder != 0)
						{
							short_data = 0;
							memcpy(&short_data, p, remainder);
							__raw_writel(short_data, fb_dev->lcdc_reg + AK_TX_PAYLOAD_REG);
						}

						// pr_err("data_type %x count %d vc %d \n", data_type, j-2, fb_dev->dsi_info->vc);
						regval = (data_type | (fb_dev->dsi_info->vc << 6) | ((j - 2) << 8));
						__raw_writel(regval, fb_dev->lcdc_reg + AK_PACKET_HEAD_REG);
						/* check if the current packet tx done */
						do
						{
						} while (!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & 0x1));
						mdelay(delay);
					}
				}
			}
			if (config == 0xFFF)
				break;
		}
		i += (j + 1);
	}
	pr_err("panel-init-list end...\n");
}

/*
 * mipi_dsi_cfg
 * configure mipi bridge
 *
 * @fb_dev:			pointor to fb device
 */
static void mipi_dsi_cfg(struct ak_fb *fb_dev)
{
	struct lcdc_dsi_info *dsi_info = fb_dev->dsi_info;

	/* check if the DPHY is ready */
#if 0
	do {
	} while (!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & (1<<2)));
#endif

	/* mipi dsi config reg setting */
	__raw_writel(dsi_info->num_lane, fb_dev->dsi_reg + AK_DSI_NUM_LANES);
	__raw_writel(dsi_info->noncontinuous_clk, fb_dev->dsi_reg + AK_DSI_NONCONTINUOUS_CLK);
	__raw_writel(dsi_info->t_pre, fb_dev->dsi_reg + AK_DSI_T_PRE);
	__raw_writel(dsi_info->t_post, fb_dev->dsi_reg + AK_DSI_T_POST);
	__raw_writel(dsi_info->tx_gap, fb_dev->dsi_reg + AK_DSI_TX_GAP);
	__raw_writel(dsi_info->autoinsert_eotp, fb_dev->dsi_reg + AK_DSI_AUTOINSERT_EOTP);
	__raw_writel(dsi_info->htx_to_count, fb_dev->dsi_reg + AK_DSI_HTX_TO_COUNT);
	__raw_writel(dsi_info->lrx_to_count, fb_dev->dsi_reg + AK_DSI_LRX_H_TO_COUNT);
	__raw_writel(dsi_info->bta_to_count, fb_dev->dsi_reg + AK_DSI_BTA_H_TO_COUNT);
	__raw_writel(dsi_info->t_wakeup, fb_dev->dsi_reg + AK_DSI_T_WAKEUP);
	__raw_writel(dsi_info->pix_payload_size, fb_dev->dsi_reg + AK_DSI_PIX_PAYPLOAD_SIZE);
	__raw_writel(dsi_info->pix_fifo_level, fb_dev->dsi_reg + AK_DSI_PIX_FIFO_SEND_LEVEL);
	__raw_writel(dsi_info->if_color_coding, fb_dev->dsi_reg + AK_DSI_IF_COLOR_CODING);
	__raw_writel(dsi_info->pix_format, fb_dev->dsi_reg + AK_DSI_PIX_FORMAT);
	__raw_writel(dsi_info->vsync_pol, fb_dev->dsi_reg + AK_DSI_VSYNC_POL);
	__raw_writel(dsi_info->hsync_pol, fb_dev->dsi_reg + AK_DSI_HSYNC_POL);
	__raw_writel(dsi_info->video_mode, fb_dev->dsi_reg + AK_DSI_VIDEO_MODE);
	__raw_writel(dsi_info->hfp, fb_dev->dsi_reg + AK_DSI_HFP);
	__raw_writel(dsi_info->hbp, fb_dev->dsi_reg + AK_DSI_HBP);
	__raw_writel(dsi_info->hsa, fb_dev->dsi_reg + AK_DSI_HSA);
	__raw_writel(dsi_info->mult_pkts_en, fb_dev->dsi_reg + AK_DSI_MULT_PKTS_EN);
	__raw_writel(dsi_info->vbp, fb_dev->dsi_reg + AK_DSI_VBP);
	__raw_writel(dsi_info->vfp, fb_dev->dsi_reg + AK_DSI_VFP);
	__raw_writel(dsi_info->bllp_mode, fb_dev->dsi_reg + AK_DSI_BLLP_MODE);
	__raw_writel(dsi_info->use_null_pkt_bllp, fb_dev->dsi_reg + AK_DSI_USE_NULL_PKT_BLLP);
	__raw_writel(dsi_info->vactive, fb_dev->dsi_reg + AK_DSI_VACTIVE);
	__raw_writel(dsi_info->vc, fb_dev->dsi_reg + AK_DSI_VC);

#if 0
	/* check if the DPHY is ready */
	do {
	} while (!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & (1<<2)));
#endif
}

/*
 * set_dsi_dphy_output_clock -
 * configure clock of mipi phy
 *
 * @fb_dev:			pointor to fb device
 */
static void set_dsi_dphy_output_clock(struct ak_fb *fb_dev)
{
	unsigned int regval;
	struct lcdc_dsi_info *dsi_info = fb_dev->dsi_info;
	int n;

	/* set mipi high speed clk */
	regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
	regval &= ~(0x1FF);
	if (dsi_info->dsi_clk <= 160)
		regval |= ((dsi_info->dsi_clk - 80) * 8 / 10); // 1.25*
	else if (dsi_info->dsi_clk <= 320)
		regval |= ((dsi_info->dsi_clk) * 2 / 5); // 2.5*
	else
		regval |= ((dsi_info->dsi_clk) / 5 + 64); // 5*

	__raw_writel(regval, AK_LCD_DSI_CLK_CFG);
	// pr_err("mipi clk reg value :%x\n", regval);

	regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
	regval |= (0x1 << 30);
	__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

	/* check if the DPHY is ready */
	n = 100000;
	do
	{
		n--;
	} while ((!(__raw_readl(fb_dev->lcdc_reg + AK_DPI_STATUS_REG) & (1 << 2))) && (n > 0));
}

/*
 * mipi_panel_init -
 * configuration of mipi lcd
 *
 * @child:			pointor to device node
 * @fb_dev:			pointor to fb device
 */
static void mipi_panel_init(struct device_node *child, struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	struct lcdc_dsi_info *dsi_info = fb_dev->dsi_info;
	struct clk *peri_clk;
	u32 peri_rate;
	u32 pclk, factor;
	u32 regval;

	of_property_read_u32(child, "panel-rgb-seq", &panel_info->rgb_seq);
	of_property_read_u32(child, "panel-width", &panel_info->width);
	of_property_read_u32(child, "panel-height", &panel_info->height);
	of_property_read_u32(child, "panel-pclk-div", &panel_info->pclk_div);

	of_property_read_u32(child, "panel-dsi-num-lane", &dsi_info->num_lane);
	of_property_read_u32(child, "panel-dsi-txd0", &dsi_info->txd0);
	of_property_read_u32(child, "panel-dsi-txd1", &dsi_info->txd1);
	of_property_read_u32(child, "panel-dsi-txd2", &dsi_info->txd2);
	of_property_read_u32(child, "panel-dsi-txd3", &dsi_info->txd3);
	of_property_read_u32(child, "panel-dsi-txd4", &dsi_info->txd4);
	of_property_read_u32(child, "panel-dsi-noncontinuous-clk", &dsi_info->noncontinuous_clk);
	of_property_read_u32(child, "panel-dsi-t-pre", &dsi_info->t_pre);
	of_property_read_u32(child, "panel-dsi-t-post", &dsi_info->t_post);
	of_property_read_u32(child, "panel-dsi-tx-gap", &dsi_info->tx_gap);
	of_property_read_u32(child, "panel-dsi-autoinsert-eotp", &dsi_info->autoinsert_eotp);
	of_property_read_u32(child, "panel-dsi-htx-to-count", &dsi_info->htx_to_count);
	of_property_read_u32(child, "panel-dsi-lrx-to-count", &dsi_info->lrx_to_count);
	of_property_read_u32(child, "panel-dsi-bta-to-count", &dsi_info->bta_to_count);
	of_property_read_u32(child, "panel-dsi-t-wakeup", &dsi_info->t_wakeup);
	of_property_read_u32(child, "panel-dsi-pix-fifo-send-level", &dsi_info->pix_fifo_level);
	of_property_read_u32(child, "panel-dsi-if-color-coding", &dsi_info->if_color_coding);
	of_property_read_u32(child, "panel-dsi-pix-format", &dsi_info->pix_format);
	of_property_read_u32(child, "panel-dsi-vsync-pol", &dsi_info->vsync_pol);
	of_property_read_u32(child, "panel-dsi-hsync-pol", &dsi_info->hsync_pol);
	of_property_read_u32(child, "panel-dsi-video-mode", &dsi_info->video_mode);
	of_property_read_u32(child, "panel-dsi-hfp", &dsi_info->hfp);
	of_property_read_u32(child, "panel-dsi-hbp", &dsi_info->hbp);
	of_property_read_u32(child, "panel-dsi-hsa", &dsi_info->hsa);
	of_property_read_u32(child, "panel-dsi-mult-pkts-en", &dsi_info->mult_pkts_en);
	of_property_read_u32(child, "panel-dsi-vbp", &dsi_info->vbp);
	of_property_read_u32(child, "panel-dsi-vfp", &dsi_info->vfp);
	of_property_read_u32(child, "panel-dsi-vsa", &dsi_info->vsa);
	of_property_read_u32(child, "panel-dsi-bllp-mode", &dsi_info->bllp_mode);
	of_property_read_u32(child, "panel-dsi-use-null-pkt-bllp", &dsi_info->use_null_pkt_bllp);
	of_property_read_u32(child, "panel-dsi-vc", &dsi_info->vc);
	of_property_read_u32(child, "panel-dsi-cmd-type", &dsi_info->cmd_type);
	of_property_read_u32(child, "panel-dsi-clk", &dsi_info->dsi_clk);

	if (dsi_info->mult_pkts_en == 0)
		dsi_info->pix_payload_size = panel_info->width;
	else if (dsi_info->mult_pkts_en == 1)
		dsi_info->pix_payload_size = panel_info->width / 2;
	dsi_info->vactive = panel_info->height;

	if (!fb_dev->lcd_ctrl_inited)
	{
		// printk(KERN_ERR"%s, line:%d\n", __func__, __LINE__);
		mipi_clk_and_pad_setting(fb_dev);
	}
	if (dsi_info->if_color_coding <= 2)
		panel_info->bus_width = 1;
	else if (dsi_info->if_color_coding <= 4)
		panel_info->bus_width = 2;
	else if (dsi_info->if_color_coding <= 5)
		panel_info->bus_width = 3;
	else
		panel_info->bus_width = 3;

#ifdef CONFIG_MACH_AK37D
	peri_clk = clk_get(NULL, "peri_pll");
#endif
#ifdef CONFIG_MACH_AK37E
	peri_clk = clk_get(NULL, "asic_pll");
#endif
	if (IS_ERR(peri_clk))
	{
		pr_err("get peri_clk err: %ld\n", PTR_ERR(peri_clk));
#ifdef CONFIG_MACH_AK37D
		peri_rate = 600;
#endif
#ifdef CONFIG_MACH_AK37E
		peri_rate = 500;
#endif
	}
	else
	{
		peri_rate = clk_get_rate(peri_clk) / MHz;
	}
	pr_err("peri_rate :%d \n", peri_rate);

	pclk = peri_rate / (panel_info->pclk_div + 1);

	// dpi_pixel_size * dpi_event_size * dpi_pclk_period = number_of_dsi_bytes * 8 * hs_bit_period / cfg_num_lanes
	// dpi_event_size = number_of_dsi_bytes*8*dpi_clk/(cfg_num_lanes*hs_clk)
	//
	// factor = (1000*8*600)/((panel_info->pclk_div + 1)*(dsi_info->num_lane+1)*dsi_info->dsi_clk);
	factor = (1000 * 8 * pclk) / ((dsi_info->num_lane + 1) * dsi_info->dsi_clk);
	pr_err("pclk %d factor %d \n", pclk, factor);

	/* get RGB timing config from dts */
	panel_info->pclk_pol = 1;
	panel_info->vsync_pol = dsi_info->vsync_pol ? 0 : 1;
	panel_info->hsync_pol = dsi_info->hsync_pol ? 0 : 1;
	panel_info->thpw = dsi_info->hsa;
	panel_info->thb = dsi_info->hbp;
	panel_info->thd = panel_info->width;
	panel_info->thf = dsi_info->hfp;
	panel_info->tvpw = dsi_info->vsa;
	panel_info->tvb = dsi_info->vbp;
	panel_info->tvd = panel_info->height;
	panel_info->tvf = dsi_info->vfp;

	/* calcuate mipi dsi timing setting */
	dsi_info->hsa = panel_info->thpw * 1000 / factor;
	dsi_info->hbp = panel_info->thb * 1000 / factor;
	dsi_info->hfp = panel_info->thf * 1000 / factor;
	dsi_info->vbp = panel_info->tvb;
	dsi_info->vfp = panel_info->tvf;

	if (!fb_dev->lcd_ctrl_inited)
	{
		// printk(KERN_ERR"%s, line:%d\n", __func__, __LINE__);
		mipi_dsi_cfg(fb_dev);

		set_dsi_dphy_output_clock(fb_dev);

		/* reset mipi lcd panel */
		if (fb_dev->reset_pin >= 0)
		{
#ifdef CONFIG_MACH_AK37E
			gpio_direction_output(fb_dev->reset_pin, !fb_dev->reset_pin_active);
			mdelay(1);
			gpio_set_value(fb_dev->reset_pin, fb_dev->reset_pin_active);
			mdelay(5);
			gpio_set_value(fb_dev->reset_pin, !fb_dev->reset_pin_active);
			mdelay(25);
#else
			gpio_direction_output(fb_dev->reset_pin, 1);
			gpio_set_value(fb_dev->reset_pin, 1);
			mdelay(1);
			gpio_set_value(fb_dev->reset_pin, 0);
			mdelay(5);
			gpio_set_value(fb_dev->reset_pin, 1);
			mdelay(25);
#endif
		}

		mipi_panel_cmd_setting(child, fb_dev);

		pr_debug("panel_info->pclk_div = %d\n", panel_info->pclk_div);
		pr_debug("panel_info->thpw = %d\n", panel_info->thpw);
		pr_debug("panel_info->thb = %d\n", panel_info->thb);
		pr_debug("panel_info->thd = %d\n", panel_info->thd);
		pr_debug("panel_info->thf = %d\n", panel_info->thf);
		pr_debug("panel_info->tvpw = %d\n", panel_info->tvpw);
		pr_debug("panel_info->tvb = %d\n", panel_info->tvb);
		pr_debug("panel_info->tvd = %d\n", panel_info->tvd);
		pr_debug("panel_info->tvf = %d\n", panel_info->tvf);

		rgb_panel_info_cfg(fb_dev);
	}
}

static void ak_lcdc_mpu_display(struct ak_fb *fb_dev)
{
	u32 regval;
	/* config dma address */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_RGB_CTRL);
	regval &= ~(0x3FFFFFFF);
	regval |= ((fb_dev->panel_info->paddr + fb_dev->panel_info->paddr_offset) & 0x3FFFFFFF);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_RGB_CTRL);

	/* set sw ctrl en */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_SW_CTRL);
	regval |= (0x1 << 12);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_SW_CTRL);

	/* rgb go */
	if (fb_dev->lcd_go == false)
	{
		if (fb_dev->panel_info->rgb_if == DISP_IF_MPU)
		{
			__raw_writel(0x1 << 3, fb_dev->lcdc_reg + AK_LCD_GO);
			fb_dev->lcd_go = true;
		}
	}
}

/*
 * mpu_set_16bit_buswidth -
 * set buswith to 16bits
 *
 * @fb_dev:			pointor to fb device
 */
static void mpu_set_16bit_buswidth(struct ak_fb *fb_dev)
{
	u32 reg_val;

	reg_val = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	reg_val &= ~(0x3 << 14);
	reg_val |= (MPU16BITS << 14);
	__raw_writel(reg_val, fb_dev->lcdc_reg + AK_LCD_MPU_1);
}

/*
 * mpu_recover_buswidth -
 * recover buswith
 *
 * @fb_dev:			pointor to fb device
 */
static void mpu_recover_buswidth(struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	u32 reg_val;

	reg_val = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	reg_val &= ~(0x3 << 14);
	reg_val |= (panel_info->data_width << 14);
	__raw_writel(reg_val, fb_dev->lcdc_reg + AK_LCD_MPU_1);
}

/*
 * wr_cycle_len - wr_low_len >= 2 (18/16bits mode)
 * wr_cycle_len/2 - wr_low_len >= 2 (8/9bits mode)
 */
static void mpu_clk_set(struct lcdc_panel_info *panel_info, struct ak_fb *fb_dev)
{
	unsigned int regval;

	/*pclk_div setting	&  pclk enable	& inform to change the pclk */

	/*write timing setting1: wr_cycle */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	regval &= ~(0x7f << 6);
	regval |= panel_info->wr_cycle << 6;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_MPU_1);

	/*write timing setting2: wr_low */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	regval &= ~(0x1f << 1);
	regval |= panel_info->wr_low << 1;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_MPU_1);
}

/*
 * mpu_panel_set -
 * set timing and so on suit for mpu
 *
 * @panel_info:		panel infomations
 * @fb_dev:			pointor to fb device
 */
static int mpu_panel_set(struct lcdc_panel_info *panel_info, struct ak_fb *fb_dev)
{
	unsigned int regval;

	if (!panel_info)
		return -1;

	/*mpu interface buswidth */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	regval &= ~(0x3 << 14);
	regval |= panel_info->data_width << 14;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_MPU_1);

	/*mpu color mode */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	regval &= ~(0x1 << 13);
	regval |= panel_info->color_mode << 13;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_MPU_1);

	/*mpu rs's polarity select */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_MPU_1);
	regval &= ~(0x1);
	regval |= panel_info->a0_pol;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_MPU_1);

	/*lcd interface mode select: MPU interface */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	regval &= ~(0x3 << 5);
	regval |= (0x1 << 5);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

	/* lcd set panel size */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_PANEL_SIZE);
	regval &= ~(0xffffffff);
	regval |= (panel_info->width << 11) | (panel_info->height);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_PANEL_SIZE);

	return 0;
}

/*
 * mpu_read_reg -
 * read register of mpu lcd
 *
 * @ctl:			is command or data
 * @data:			data to read
 * @count:			number of data
 * @fb_dev:			pointor to fb device
 */
static void mpu_read_reg(int ctl, unsigned int *data, unsigned count, struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	unsigned int regval, i;

	/* write command*/
	regval = __raw_readl(fb_dev->lcdc_reg + AK_REG_CONFIGURE);
	regval &= ~(0xffffffff);
	regval = (ctl | (0 << 24));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_REG_CONFIGURE);
	udelay(50);

	for (i = 0; i < count; i++)
	{
		/* set all signal to high */
		regval = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ);
		regval = ((0 << 29) | (1 << 27) | (1 << 26) | (1 << 24));
		__raw_writel(regval, fb_dev->lcdc_reg + AK_MPU_READ);
		udelay(100);

		/* set cs signal to low*/
		regval = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ);
		regval = ((0 << 29) | (1 << 27) | (0 << 26) | (1 << 24));
		__raw_writel(regval, fb_dev->lcdc_reg + AK_MPU_READ);
		udelay(100);

		/* set RD to LOW*/
		regval = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ);
		regval = ((0 << 29) | (0 << 27) | (0 << 26) | (1 << 24));
		__raw_writel(regval, fb_dev->lcdc_reg + AK_MPU_READ);
		udelay(100);

		/*set RD to HIGH*/
		regval = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ);
		regval = ((0 << 29) | (1 << 28) | (1 << 27) | (0 << 26) | (1 << 24));
		__raw_writel(regval, fb_dev->lcdc_reg + AK_MPU_READ);
		udelay(100);

		/*Clear 28 bit and end reading data. */
		regval = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ);
		regval = ((0 << 29) | (1 << 27) | (0 << 26) | (1 << 24));
		__raw_writel(regval, fb_dev->lcdc_reg + AK_MPU_READ);
		udelay(100);

		/* read data*/
		data[i] = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ) & 0xffffff;

		/* set all signal to high */
		regval = __raw_readl(fb_dev->lcdc_reg + AK_MPU_READ);
		regval = ((1 << 29) | (1 << 27) | (1 << 26) | (1 << 24));
		__raw_writel(regval, fb_dev->lcdc_reg + AK_MPU_READ);
		udelay(100);
	}
}

/*
 * mpu_write_reg -
 * write register of mpu lcd
 *
 * @ctl:			is command or data
 * @data:			data to write
 * @fb_dev:			pointor to fb device
 */
static int mpu_write_reg(int ctl, unsigned int data, struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	unsigned int regval;

	mpu_set_16bit_buswidth(fb_dev);
	if (LCD_MPU_CMD == ctl)
	{
		__raw_writel(AK_LCD_MPU_CMD | data, fb_dev->lcdc_reg + AK_REG_CONFIGURE);
		udelay(50);
	}
	else if (LCD_MPU_DATA == ctl)
	{
		__raw_writel(AK_LCD_MPU_DATA | data, fb_dev->lcdc_reg + AK_REG_CONFIGURE);
		udelay(50);
	}

	mpu_recover_buswidth(fb_dev);
	return 0;
}

/*
 * mpu_pclk_reset-
 * configure all clocks relate to mpu
 *
 * @fb_dev:			pointor to fb device
 */
static void mpu_pclk_reset(struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	struct lcdc_rgb_info *rgb_info = fb_dev->rgb_info;

	u32 regval;
	unsigned long flags;

	/* open lcd controller clock*/
	regval = __raw_readl(AK_LCD_CLOCK_GATE);
	regval &= ~(0x1 << 22);
	__raw_writel(regval, AK_LCD_CLOCK_GATE);

	/* lcd controller reset*/
	regval = __raw_readl(AK_LCD_SW_RESET_CTRL);
	regval |= (0x1 << 22);
	__raw_writel(regval, AK_LCD_SW_RESET_CTRL);
	mdelay(1);
	regval = __raw_readl(AK_LCD_SW_RESET_CTRL);
	regval &= ~(0x1 << 22);
	__raw_writel(regval, AK_LCD_SW_RESET_CTRL);

	/* lcd sw ctrl settings */
	rgb_info->alert_line = panel_info->height - rgb_info->alert_line;
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_SW_CTRL);
	regval &= ~((0xFFF));
	regval |= ((rgb_info->alert_line) | (0x0 << 11));
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_SW_CTRL);

	/* pclk domain reset */
	local_irq_save(flags);
	lcdc_set_reg(1, AK_LCD_PCLK_CTRL, 30, 30);
	lcdc_set_reg(0, AK_LCD_PCLK_CTRL, 30, 30);
	local_irq_restore(flags);

	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_PCLK);
	regval |= (1 << 8 | ((panel_info->pclk_div) << 1) | 1);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_PCLK);
}

/*
 * mpu_panel_cmd_setting -
 * send comman to mpu lcd to initialled
 *
 * @child:			pointor to device node
 * @fb_dev:			pointor to fb device
 */
static void mpu_panel_cmd_setting(struct device_node *child, struct ak_fb *fb_dev)
{
	struct property *panel_init;
	int num_inits;
	u32 config, err;
	u32 regval;
	int i, j;

	panel_init = of_find_property(child, "panel-init-list", NULL);
	num_inits = panel_init ? (panel_init->length / 4) : 0;

	j = 0;
	for (i = 0; i < num_inits; i++)
	{
		err = of_property_read_u32_index(child, "panel-init-list", i, &config);
		if (err)
		{
			break;
			pr_err("panel-init-list get index:%d failed\n", i);
		}

		/*0xFFF means end of data flag, and next will be a command.*/
		if (config == 0xFFF)
		{
			j = 0;
			continue;
		}

		if (j == 0)
		{
			mpu_write_reg(LCD_MPU_CMD, config, fb_dev);
			if (config == 0x11)
			{
				msleep(120); /* Delay 120ms*/
			}
		}
		else
		{
			mpu_write_reg(LCD_MPU_DATA, config, fb_dev);
		}
		j++;
	}
}

/*
 * mpu_reflash_for_open_pclk -
 *
 * @fb_dev:			pointor to fb device
 */
static void mpu_reflash_for_open_pclk(struct ak_fb *fb_dev)
{
	u32 status = 0;
	u32 regval;

	/* disable the RGB, YUV1, YUV2, OSD channel */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	regval &= (~0xf);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

	/* set background color */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_BACKGROUND);
	regval = 0xffffff;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_BACKGROUND);

	/*mpu reflash start command */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_GO);
	regval = (1 << 3) | (0 << 2) | 0;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_GO);

	while (1)
	{
		status = __raw_readl(fb_dev->lcdc_reg + AK_LCD_INT_STATUS);
		if ((status & (1 << 18)) == (1 << 18))
			pr_info("fifo alarm!\n");

		if ((status & (1 << 17)) == (1 << 17))
			pr_info("alert valid!\n");

		if ((status & (1 << 2)) == (1 << 2))
		{
			pr_info("display ok!\n");
			break;
		}
	}
}

/*
 * mpu_scan_dir -
 * set scanning method of mpu lcd
 *
 * @fb_dev:			pointor to fb device
 * @dir:			scanning method
 */
static void mpu_scan_dir(struct ak_fb *fb_dev, u32 dir)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	u16 regval = 0;
	u8 dirreg = 0;
	u16 temp;
	u16 width;
	u16 height;

	width = (u16)panel_info->width;
	height = (u16)panel_info->height;

	/*switch direction. */
	switch (dir)
	{
	case 0:
		dir = 6;
		break;
	case 1:
		dir = 7;
		break;
	case 2:
		dir = 4;
		break;
	case 3:
		dir = 5;
		break;
	case 4:
		dir = 1;
		break;
	case 5:
		dir = 0;
		break;
	case 6:
		dir = 3;
		break;
	case 7:
		dir = 2;
		break;
	}
	switch (dir)
	{
	case L2R_U2D: /*from left to right,from top to bottom. */
		regval |= (0 << 7) | (0 << 6) | (0 << 5);
		break;
	case L2R_D2U: /*from left to right,from bottom to top. */
		regval |= (1 << 7) | (0 << 6) | (0 << 5);
		break;
	case R2L_U2D: /*from right to left,from top to bottom. */
		regval |= (0 << 7) | (1 << 6) | (0 << 5);
		break;
	case R2L_D2U: /*from right to left,from bottom to top. */
		regval |= (1 << 7) | (1 << 6) | (0 << 5);
		break;
	case U2D_L2R: /*from top to bottom, from left to right*/
		regval |= (0 << 7) | (0 << 6) | (1 << 5);
		break;
	case U2D_R2L: /*from top to bottom, from right to left*/
		regval |= (0 << 7) | (1 << 6) | (1 << 5);
		break;
	case D2U_L2R: /*from bottom to top, from left to right*/
		regval |= (1 << 7) | (0 << 6) | (1 << 5);
		break;
	case D2U_R2L: /*from bottom to top, from right to left*/
		regval |= (1 << 7) | (1 << 6) | (1 << 5);
		break;
	}
	dirreg = MPU_DIRECTION_REG;

	regval |= 0x08;

	mpu_write_reg(AK_LCD_MPU_CMD, dirreg, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, regval, fb_dev);

	mpu_write_reg(AK_LCD_MPU_CMD, panel_info->setxcmd, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, 0, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, 0, fb_dev);

	mpu_write_reg(AK_LCD_MPU_DATA, (width - 1) >> 8, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, (width - 1) & 0XFF, fb_dev);

	mpu_write_reg(AK_LCD_MPU_CMD, panel_info->setycmd, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, 0, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, 0, fb_dev);

	mpu_write_reg(AK_LCD_MPU_DATA, (height - 1) >> 8, fb_dev);
	mpu_write_reg(AK_LCD_MPU_DATA, (height - 1) & 0XFF, fb_dev);
}

/*
 * mpu_display_dir -
 * set scanning method of mpu lcd, and get scanning mothod command
 *
 * @fb_dev:			pointor to fb device
 * @dir:			scanning method
 */
static void mpu_display_dir(struct ak_fb *fb_dev, u32 dir)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	u32 default_scan_dir = 0;
	u32 tmp = 0;

	/* portrait*/
	if (dir == 0)
	{
		panel_info->dir = 0;

		panel_info->wramcmd = 0X2C;
		panel_info->setxcmd = 0X2A;
		panel_info->setycmd = 0X2B;
		// DFT_SCAN_DIR=U2D_R2L;
		default_scan_dir = L2R_U2D;
	}
	else /* landscape */
	{
		panel_info->dir = 1;

		panel_info->wramcmd = 0X2C;
		panel_info->setxcmd = 0X2A;
		panel_info->setycmd = 0X2B;
		default_scan_dir = U2D_L2R;
	}
	mpu_scan_dir(fb_dev, default_scan_dir);
}

/**
 * @name lcd_reflash_RGB_color
 * @brief display only background color
 */
void mpu_reflash_RGB_color(u32 color_val, struct ak_fb *fb_dev)
{
	u32 status = 0;
	u32 regval;

	// disable the RGB, YUV1, YUV2, OSD channel
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	regval &= (~0xf);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

	// set background color
	regval = __raw_readl(fb_dev->lcdc_reg + AK_RGB_BACKGROUND);
	regval = color_val;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_RGB_BACKGROUND);

	/*send writting MPU GRAM command: 0x2c*/
	mpu_write_reg(LCD_MPU_CMD, MPU_WRITE_DATA_CMD, fb_dev);

	/*mpu reflash start command */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_GO);
	regval = (1 << 3) | (0 << 2) | 0;
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_GO);

	while (1)
	{
		status = __raw_readl(fb_dev->lcdc_reg + AK_LCD_INT_STATUS);
		if ((status & (1 << 18)) == (1 << 18))
			pr_info("fifo alarm!\n");

		if ((status & (1 << 17)) == (1 << 17))
			pr_info("alert valid!\n");

		if ((status & (1 << 2)) == (1 << 2))
		{
			pr_info("display ok!\n");
			break;
		}
	}
}

/*
 * mpu_panel_readid -
 * read mpu lcd ID
 *
 * @fb_dev:			pointor to fb device
 */
static void mpu_panel_readid(struct ak_fb *fb_dev)
{
	u32 id_dat[4] = {0};
	u32 id_val = 0;

	mpu_set_16bit_buswidth(fb_dev);
	mpu_read_reg(0x04, id_dat, 4, fb_dev);
	mpu_recover_buswidth(fb_dev);

	id_dat[1] &= 0xff;
	id_dat[2] &= 0xff;
	id_dat[3] &= 0xff;

	id_val = ((id_dat[1] << 16) | (id_dat[2] << 8) | (id_dat[3]));

	pr_err("MPU PANEL ID = %x\n", id_val);
	pr_err("Manufacture ID= %x\n", id_dat[1]);
	pr_err("DriverVersion ID= %x\n", id_dat[2]);
	pr_err("Driver ID= %x\n", id_dat[3]);
}

/*
 * mpu_panel_init -
 * initial mpu lcd
 *
 * @child:			pointor to  device node
 * @fb_dev:			pointor to fb device
 */
static void mpu_panel_init(struct device_node *child, struct ak_fb *fb_dev)
{
	struct lcdc_panel_info *panel_info = fb_dev->panel_info;
	u32 regval;

	of_property_read_u32(child, "panel-width", &panel_info->width);
	of_property_read_u32(child, "panel-height", &panel_info->height);
	of_property_read_u32(child, "panel-pclk-div", &panel_info->pclk_div);
	of_property_read_u32(child, "panel-data-width", &panel_info->data_width);
	of_property_read_u32(child, "panel-color-mode", &panel_info->color_mode);
	of_property_read_u32(child, "panel-rs-polarity", &panel_info->a0_pol);
	of_property_read_u32(child, "panel-wr-cycle", &panel_info->wr_cycle);
	of_property_read_u32(child, "panel-wr-low", &panel_info->wr_low);
	of_property_read_u32(child, "panel-direction", &panel_info->panel_direction);

	if (fb_dev->lcd_ctrl_inited)
	{
		printk(KERN_ERR "%s, line:%d\n", __func__, __LINE__);
		return;
	}

	mpu_pclk_reset(fb_dev);
	mpu_clk_set(panel_info, fb_dev);
	mpu_panel_set(panel_info, fb_dev);

	/* reset mpu lcd panel */
	if (fb_dev->reset_pin >= 0)
	{
#ifdef CONFIG_MACH_AK37E
		gpio_direction_output(fb_dev->reset_pin, !fb_dev->reset_pin_active);
		mdelay(10); /* Delay 10ms*/
		gpio_set_value(fb_dev->reset_pin, fb_dev->reset_pin_active);
		mdelay(10); /* Delay 10ms*/
		gpio_set_value(fb_dev->reset_pin, !fb_dev->reset_pin_active);
		mdelay(120); /* Delay 120ms*/
#else
		gpio_direction_output(fb_dev->reset_pin, 1);
		gpio_set_value(fb_dev->reset_pin, 1);
		mdelay(10); /* Delay 10ms*/
		gpio_set_value(fb_dev->reset_pin, 0);
		mdelay(10); /* Delay 10ms*/
		gpio_set_value(fb_dev->reset_pin, 1);
		mdelay(120); /* Delay 120ms*/
#endif
	}

	/*根据芯片设计的信息，需要GO刷屏后才能打开PCLK，命令波形才能正常输出*/
	mpu_reflash_for_open_pclk(fb_dev);
	/*read mpu panel ID */
	mpu_panel_readid(fb_dev);
	/*mpu panel setting.*/
	mpu_panel_cmd_setting(child, fb_dev);
}

/*
 * lcdc_info_cfg -
 * parse node info and initial controller & panel
 *
 * @child:			pointor to  device node
 * @fb_dev:			pointor to fb device
 */
static void lcdc_info_cfg(struct device_node *child, struct ak_fb *fb_dev)
{
	if (fb_dev->panel_info->rgb_if == DISP_IF_RGB)
	{
		rgb_panel_init(child, fb_dev);
	}
	else if (fb_dev->panel_info->rgb_if == DISP_IF_DSI)
	{
		struct device *dev = &fb_dev->pdev->dev;
		struct lcdc_dsi_info *dsi_info;

		dsi_info = devm_kzalloc(dev, sizeof(struct lcdc_dsi_info), GFP_KERNEL);
		if (!dsi_info)
			pr_err("alloc lcdc dsi info buffer error\n");
		else
		{
			pr_err("alloc lcdc mipi_panel_init\n");
			fb_dev->dsi_info = dsi_info;
			mipi_panel_init(child, fb_dev);
		}
	}
	else if (fb_dev->panel_info->rgb_if == DISP_IF_MPU)
	{
		/* initial mpu panel */
		mpu_panel_init(child, fb_dev);
	}
}

/******************************************
 * IRQ HANDLE
 ******************************************/
static void irq_work_func(struct work_struct *work)
{
	struct ak_fb *fb_dev = container_of(work, struct ak_fb, fb_irq_work.work);
	ak_fb_sys_err_recover(fb_dev);
}

/**
 * handle the alert done interrupt, new parameter can be prepared for the next frame
 */
static inline void handle_update_done_irq(struct ak_fb *fb_dev)
{
	/* alert done, set new parameter can be prepared for the next frame */
	if (fb_dev->par_change == true)
	{
		fb_dev->par_change = false;
	}
}

/*
 * Set the irq enable mask
 */
static void ak_lcdc_set_irq_mask(struct ak_fb *fb_dev)
{
	u32 regval;

	regval = IRQ_DPI_HOST_ERR
			 //|IRQ_BUF_EMPTY
			 //|IRQ_ALERT_VALID
			 | IRQ_UPDATE_DONE
			 //|IRQ_RGB_FRAME_START
			 //|IRQ_RGB_FRAME_END
			 //|IRQ_MPU_FRAME_END
			 //|IRQ_MPU_FRAME_START
			 | IRQ_SYS_ERROR;

	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_INT_MASK);
}

/**
 * @brief     handle the ak37c lcd interrupt
 *            handle the sys_error interrupt only now
 * @author    Feilong Dong
 * @date      2019-04-16
 * @param     [in]  irq :  the irq number
 * @param     [in]  dev_id :  private data struction of the ak37c lcd driver
 * @return    irqreturn_t :  return IRQ_HANDLED
 * @retval    IRQ_HANDLED :  irq handle successful.
 */
static irqreturn_t ak_fb_irq_handler(int irq, void *dev_id)
{
	struct ak_fb *fb_dev = dev_id;
	struct device *dev = &(fb_dev->pdev->dev);
	u32 irq_status;

	// pr_err("ak_fb_irq_handler enter !!!\n");
	irq_status = __raw_readl(fb_dev->lcdc_reg + AK_LCD_INT_STATUS);

	if (irq_status & IRQ_SYS_ERROR)
	{
		dev_err(dev, "LCD SYS error@INTF %d\n", fb_dev->panel_info->rgb_if);
		schedule_delayed_work(&fb_dev->fb_irq_work, 0);
	}

	if (irq_status & IRQ_MPU_FRAME_START)
	{
		/* nothing todo */
	}

	if (irq_status & IRQ_MPU_FRAME_END)
	{
		/* nothing todo */
	}

	if (irq_status & IRQ_RGB_FRAME_START)
	{
		/* nothing todo */
	}

	if (irq_status & IRQ_RGB_FRAME_END)
	{
		/* nothing todo */
	}

	if (irq_status & IRQ_ALERT_VALID)
	{
		// dev_err(dev, "LCD controller alert valid\n");
		/* nothing todo */
	}

	if (irq_status & IRQ_UPDATE_DONE)
	{
		// dev_err(dev, "LCD controller alert done\n");
		handle_update_done_irq(fb_dev);
	}

	if (irq_status & IRQ_BUF_EMPTY)
	{
		// dev_err(dev, "LCD controller buf empty\n");
		pr_debug("rgb_go = 0x%x, lcd_satus = 0x%x, top_config = 0x%x\n",
				 __raw_readl(fb_dev->lcdc_reg + AK_LCD_GO),
				 __raw_readl(fb_dev->lcdc_reg + AK_LCD_INT_STATUS),
				 __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG));
		/* nothing todo */
	}

	if (irq_status & IRQ_DPI_HOST_ERR)
	{
		dev_err(dev, "LCD DPI host err\n");
		/* todo */
	}
	return IRQ_HANDLED;
}

#if 0

static int ak_load_logo_mtd(struct device *dev, void *logo_vaddr)
{
	int ret = 0;
	int retvalue = 0;
	void * handle ;
	unsigned long logo_len = 0;

	/* search the same name partition */
	handle = partition_open(LOGO_FILE_NAME);
	if (handle == NULL) {
		dev_err(dev, "%s, open logo partition error!\n", __func__);
		goto load_logo_exit;
	}

	/* get logo data len& read out */
	logo_len = partition_get_data_size(handle);
	ret = partition_read(handle , (unsigned char *)logo_vaddr, logo_len);
	if (ret == -1) {
		dev_err(dev, "%s, read logo partition error!\n", __func__);
		retvalue = -EIO;
	}

	/* olose the same name partition */
	ret = partition_close(handle);
	if (ret == -1) {
		dev_err(dev, "%s, close logo partition error!\n", __func__);
		retvalue = -EIO;
	}

	return retvalue;
load_logo_exit:
	return -EIO;
}

#else
static int ak_load_logo_mtd(struct device *dev, void *logo_vaddr, unsigned long max_len)
{
	int ret = 0;
	int retvalue = 0;
	void *handle;
	unsigned long logo_len = max_len;
	unsigned char *buf = (unsigned char *)logo_vaddr;

	ret = get_partition_data(LOGO_FILE_NAME, (unsigned char *)logo_vaddr, &logo_len);
	if (ret == -1)
	{
		dev_err(dev, "%s, get_partition_data error!\n", __func__);
		retvalue = -EIO;
	}

	return retvalue;
}

#endif

/*
 * fill_fb - fill fb_info
 * @fb_dev: private structure
 * @fb:		fb object
 * @shadow:	shadow fb or not
 */
static void fill_fb(struct ak_fb *fb_dev,
					struct lcdc_panel_info *panel_info,
					struct lcdc_rgb_input_info *rgb_input,
					struct fb_info *fb,
					int shadow)
{
	if (!shadow)
		strncpy(fb->fix.id, "ak-lcd", sizeof(fb->fix.id));
	else
		strncpy(fb->fix.id, "ak-lcd-shadow", sizeof(fb->fix.id));

	if (!shadow)
	{
		fb->fix.smem_start = panel_info->paddr;
		fb->fix.smem_len = panel_info->size * (fb_dev->fb_type + 1);
	}
	else
	{
		int fb_sd_dmasize = fb_shadow_dmasize + fb_dev->fb_adj_size;

		fb->fix.smem_start = panel_info->paddr_shadow;
		fb->fix.smem_len = fb_sd_dmasize;
	}

	fb->fix.type = FB_TYPE_PACKED_PIXELS;
	fb->fix.type_aux = 0;
	fb->fix.visual = FB_VISUAL_TRUECOLOR;
	fb->fix.xpanstep = 1;
	fb->fix.ypanstep = 1;
	fb->fix.ywrapstep = 0;
	fb->fix.line_length = panel_info->width * rgb_input->bpp;
	fb->fix.mmio_start = 0;
	fb->fix.mmio_len = 0;
	fb->fix.accel = FB_ACCEL_NONE;
	fb->fix.capabilities = FB_CAP_FOURCC;
	if (!shadow)
		fb->fix.reserved[0] = fb_dev->fb_type;
	else
		fb->fix.reserved[0] = 0;

	/*notes: the same screan buffer & size for shadow fb*/
	fb->screen_buffer = (char __iomem *)panel_info->vaddr;
	fb->screen_size = panel_info->size;
	if (fb->screen_base == NULL)
		pr_err("iomem address is null\n");

	/* set var info */
	fb->var.xres = fb_dev->rgb_input->width;
	fb->var.yres = fb_dev->rgb_input->height;
	fb->var.xres_virtual = fb_dev->panel_info->width;
	fb->var.yres_virtual = fb_dev->panel_info->height;
	fb->var.xoffset = 0;
	fb->var.yoffset = 0;
	fb->var.bits_per_pixel = rgb_input->bpp * 8;
	fb->var.grayscale = 0;
	fb->var.nonstd = 0;
	fb->var.activate = FB_ACTIVATE_NOW;
	fb->var.height = -1;
	fb->var.width = -1;

	fb->var.sync = 0;
	fb->var.vmode = FB_VMODE_NONINTERLACED;
	fb->var.rotate = 0;
	if (!shadow)
		fb->var.reserved[0] = fb_dev->fb_id;
	else
		fb->var.reserved[0] = 0;

	ak_fb_set_bitfields(&fb->var, rgb_input->rgb_seq);

	if (!shadow)
		fb->fbops = &fb_dev_ops;
	else
		fb->fbops = &fb_shadow_dev_ops;
	fb->flags = FBINFO_FLAG_DEFAULT | FBINFO_PARTIAL_PAN_OK | FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;
	/* used by .fb_fillrect and .fb_imgblt */
	fb->pseudo_palette = fb_dev->pseudo_palette;
}

int lcd_panel_type_optional(struct device_node *node)
{
	enum of_gpio_flags gpio_flags;
	int cs1_pin;
	int cs2_pin;
	int cs1_pin_active;
	int cs2_pin_active;

	if ((cs1_pin = of_get_named_gpio_flags(node, "cs1-gpio", 0, &gpio_flags)) < 0)
		printk("fb cs1 pin %d error\n", cs1_pin);
	else
	{
		cs1_pin_active = !(gpio_flags & 1);
		printk("fb cs1 pin %d,active %d\n", cs1_pin, cs1_pin_active);
	}

	if ((cs2_pin = of_get_named_gpio_flags(node, "cs2-gpio", 0, &gpio_flags)) < 0)
		printk("fb cs2 pin %d error\n", cs2_pin);
	else
	{
		cs2_pin_active = !(gpio_flags & 1);
		printk("fb cs2 pin %d,active %d\n", cs2_pin, cs2_pin_active);
	}

	if (cs1_pin >= 0 && cs2_pin >= 0)
	{
		int cs1_pin_level;
		int cs2_pin_level;

		gpio_direction_input(cs1_pin);
		gpio_direction_input(cs2_pin);

		cs1_pin_level = gpio_get_value(cs1_pin);
		cs2_pin_level = gpio_get_value(cs2_pin);

		printk("fb cs1 pin %d,level %d\n", cs1_pin, cs1_pin_level);
		printk("fb cs2 pin %d,level %d\n", cs2_pin, cs2_pin_level);
		if (cs1_pin_level && cs2_pin_level)
		{
			return 0;
		}
		else if (!cs1_pin_level && cs2_pin_level)
		{
			return 1;
		}
		else if (!cs1_pin_level && !cs2_pin_level)
		{
			return 2;
		}
		else
		{
			return 3;
		}
	}
	return -1;
}

static int ak_fb_probe(struct platform_device *pdev)
{
	struct ak_fb *fb_dev;
	struct fb_info *fb;
	struct fb_info *fb_sd;
	struct device *dev = &pdev->dev;
	struct lcdc_rgb_input_info *rgb_input;
	struct lcdc_rgb_info *rgb_info;
	struct lcdc_panel_info *panel_info;
	struct device_node *node = dev->of_node;
	struct device_node *child;
	struct resource iomem;
	u32 regval;
	int x, y;
	int fb_adj_line;
	int ret;
	enum of_gpio_flags gpio_flags;

	fb = framebuffer_alloc(sizeof(struct ak_fb), dev);
	if (fb == NULL)
	{
		dev_err(dev, "alloc framebuffer error\n");
		return -ENOMEM;
	}

	if (fb_shadow)
	{
		/*
		 * malloc for shadow fb
		 * do not malloc for ak_fb, that had malloc in fb0
		 */
		fb_sd = framebuffer_alloc(0, dev);
		if (fb_sd == NULL)
		{
			dev_err(dev, "alloc shadow framebuffer error\n");
			framebuffer_release(fb);
			return -ENOMEM;
		}
	}

	fb_dev = fb->par;
	fb_dev->fb = fb;

	if (fb_shadow)
	{
		/*store the private structure into shadow fb*/
		fb_sd->par = fb_dev;
		/*save shadow fb*/
		fb_dev->fb_shadow = fb_sd;
	}

	platform_set_drvdata(pdev, fb_dev);
	fb_dev->pdev = pdev;

	rgb_input = devm_kzalloc(dev, sizeof(struct lcdc_rgb_input_info), GFP_KERNEL);
	rgb_info = devm_kzalloc(dev, sizeof(struct lcdc_rgb_info), GFP_KERNEL);
	panel_info = devm_kzalloc(dev, sizeof(struct lcdc_panel_info), GFP_KERNEL);
	if ((!rgb_input) || (!rgb_info) || (!panel_info))
	{
		dev_err(dev, "alloc lcdc info buffer error\n");
		ret = -ENOMEM;
		goto resource_err;
	}
	fb_dev->rgb_input = rgb_input;
	fb_dev->rgb_info = rgb_info;
	fb_dev->panel_info = panel_info;

	ret = of_address_to_resource(node, 0, &iomem);
	if (ret)
	{
		dev_err(dev, "could not get IO-0 memory\n");
		goto resource_err;
	}

	fb_dev->lcdc_reg = devm_ioremap_resource(dev, &iomem);
	if (IS_ERR(fb_dev->lcdc_reg))
	{
		ret = PTR_ERR(fb_dev->lcdc_reg);
		goto resource_err;
	}

	ret = of_address_to_resource(node, 1, &iomem);
	if (ret)
	{
		dev_err(dev, "could not get IO-1 memory\n");
		goto resource_err;
	}

	fb_dev->dsi_reg = devm_ioremap_resource(dev, &iomem);
	if (IS_ERR(fb_dev->dsi_reg))
	{
		ret = PTR_ERR(fb_dev->dsi_reg);
		goto resource_err;
	}

	fb_dev->lcdc_clk = devm_clk_get(&pdev->dev, "lcdc");
	if (IS_ERR(fb_dev->lcdc_clk))
	{
		ret = PTR_ERR(fb_dev->lcdc_clk);
		goto resource_err;
	}

	fb_dev->dsi_clk = devm_clk_get(&pdev->dev, "mipi_dsi");
	if (IS_ERR(fb_dev->dsi_clk))
	{
		ret = PTR_ERR(fb_dev->dsi_clk);
		goto resource_err;
	}

	// printk("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&:%d\n",__LINE__);
	/* get phy reset gpio and reset phy */

#ifdef CONFIG_MACH_AK37E
	fb_dev->reset_pin = of_get_named_gpio_flags(node, "reset-pins", 0, &gpio_flags);
	fb_dev->reset_pin_active = !(gpio_flags & 1);
	if (fb_dev->reset_pin < 0)
		pr_err("fb reset pin %d error\n", fb_dev->reset_pin);
	else
		pr_err("fb reset pin %d,active %d\n", fb_dev->reset_pin, fb_dev->reset_pin_active);
#else
	fb_dev->reset_pin = of_get_named_gpio(node, "reset-pins", 0);
	if (fb_dev->reset_pin < 0)
		pr_err("fb reset pin %d error\n", fb_dev->reset_pin);
	else
		pr_err("fb reset pin %d \n", fb_dev->reset_pin);
#endif

#ifdef CONFIG_MACH_AK37E
	fb_dev->power_pin = of_get_named_gpio_flags(node, "pwr-gpio", 0, &gpio_flags);
	fb_dev->power_pin_active = !(gpio_flags & 1);
	if (fb_dev->power_pin < 0)
		pr_err("fb power pin %d error", fb_dev->power_pin);
	else
		pr_err("fb power pin %d,value %d,active %d\n", fb_dev->power_pin, gpio_get_value(fb_dev->power_pin), fb_dev->power_pin_active);

	fb_dev->backlight_pin = of_get_named_gpio_flags(node, "backlight-gpio", 0, &gpio_flags);
	fb_dev->backlight_pin_active = !(gpio_flags & 1);
	if (fb_dev->backlight_pin < 0)
		pr_err("fb backlight pin %d error", fb_dev->backlight_pin);
	else
		pr_err("fb backlight pin %d,value %d,active %d\n", fb_dev->backlight_pin, gpio_get_value(fb_dev->backlight_pin), fb_dev->backlight_pin_active);
#endif

	fb_dev->irq = platform_get_irq(pdev, 0);
	if (fb_dev->irq < 0)
	{
		ret = fb_dev->irq;
		goto resource_err;
	}
	pr_err("ak_fb_probe irq: %d\n", fb_dev->irq);

	INIT_DELAYED_WORK(&fb_dev->fb_irq_work, irq_work_func);

	/*
	 * check lcd controller if config or not?
	 * check bit[3](rgb_channel_en) and bit[6:5](lcd_if_mode)
	 * default bit[3]=0,disable, bit[6:5]=00, reserved
	 */
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	if (((regval >> 3) & 0x1) && ((regval >> 5) & 0x3) && (!lcd_ctl_force_init))
	{
		fb_dev->lcd_ctrl_inited = 1;
		printk(KERN_ERR "lcd ctrl has inited!\n");
	}

	if (!fb_dev->lcd_ctrl_inited)
	{
		clk_prepare_enable(fb_dev->lcdc_clk);
		fb_dev->lcd_go = false;
	}
	else
	{
		fb_dev->lcd_go = true;
	}
	fb_dev->par_change = false;

#ifdef CONFIG_MACH_AK37E
	if (!fb_dev->lcd_ctrl_inited)
	{
		gpio_direction_output(fb_dev->backlight_pin, !fb_dev->backlight_pin_active);

		mdelay(20);
		gpio_direction_output(fb_dev->power_pin, fb_dev->power_pin_active);
	}
#endif
	printk("[%d]WUQIANSHI!!!!!!!!!!!!!!!!!!!!!!!!!!!!111111\n\n\n\n\n", __LINE__);
	/* get lcd fb type, 0:single buffer, 1:double buffer */
	of_property_read_u32(node, "lcd-fb-type", &fb_dev->fb_type);

	/* get lcdc rgb config info from dts */
	of_property_read_u32(node, "lcd-logo-width", &rgb_input->width);
	of_property_read_u32(node, "lcd-logo-height", &rgb_input->height);
	of_property_read_u32(node, "lcd-logo-fmt0", &rgb_input->fmt0);
	of_property_read_u32(node, "lcd-logo-fmt1", &rgb_input->fmt1);
	of_property_read_u32(node, "lcd-logo-rgb-seq", &rgb_input->rgb_seq);
	of_property_read_u32(node, "lcd-logo-a-location", &rgb_input->a_location);

	of_property_read_u32(node, "lcd-alarm-empty", &rgb_info->alarm_empty);
	of_property_read_u32(node, "lcd-alarm-full", &rgb_info->alarm_full);
	of_property_read_u32(node, "lcd-dma-mode", &rgb_info->dma_mode);
	of_property_read_u32(node, "lcd-dummy-seq", &rgb_info->dummy_seq);
	of_property_read_u32(node, "lcd-rgb-seq-sel", &rgb_info->rgb_seq_sel);
	of_property_read_u32(node, "lcd-rgb-odd-seq", &rgb_info->rgb_odd_seq);
	of_property_read_u32(node, "lcd-rgb-even-seq", &rgb_info->rgb_even_seq);
	of_property_read_u32(node, "lcd-rgb-tpg-sel", &rgb_info->tpg_mode);
	of_property_read_u32(node, "lcd-vpage-en", &rgb_info->vpage_en);
	of_property_read_u32(node, "lcd-blank-sel", &rgb_info->blank_sel);
	of_property_read_u32(node, "lcd-pos-blank-level", &rgb_info->pos_blank_level);
	of_property_read_u32(node, "lcd-neg-blank-level", &rgb_info->neg_blank_level);
	of_property_read_u32(node, "lcd-rgb-bg-color", &rgb_info->bg_color);
	of_property_read_u32(node, "lcd-vh-delay-en", &rgb_info->vh_delay_en);
	of_property_read_u32(node, "lcd-vh-delay-cycle", &rgb_info->vh_delay_cycle);
	of_property_read_u32(node, "lcd-v-unit", &rgb_info->v_unit);
	of_property_read_u32(node, "lcd-alert-line", &rgb_info->alert_line);

/* get the lcd panel info from the dts */
/* parse child dts node, get the panel info and config list */
#if 1 && defined(CONFIG_MACH_AK37E)
	{
		int panel_type;
		panel_type = lcd_panel_type_optional(node);
		printk("lcd_panel_type_optional type:%d\n", panel_type);
		for ((child = of_get_next_available_child(node, NULL)); child != NULL; (child = of_get_next_available_child(node, child)))
		{
			of_property_read_string(child, "compatible", &panel_info->name);
			of_property_read_u32(child, "panel-if", &panel_info->rgb_if);
			lcdc_info_cfg(child, fb_dev);
			printk("Node one found!!!! compatible:%s\n", panel_info->name);
			if (panel_type == -1)
			{
				printk("Screen parameter control pin not found!!!! default compatible:%s\n", panel_info->name);
				break;
			}
			else if (!(panel_type--))
			{
				printk("Node successfully found!!!! compatible:%s\n", panel_info->name);
				strcpy(panel_compatible, panel_info->name);
				break;
			}
		}
	}

#else
	for_each_available_child_of_node(node, child)
	{
		of_property_read_string(child, "compatible", &panel_info->name);
		of_property_read_u32(child, "panel-if", &panel_info->rgb_if);
		lcdc_info_cfg(child, fb_dev);
	}
#endif
	printk("LCD USE NODE COMPATIBLE:%s\n", panel_info->name);
	/* for bootup logo display, alloc framebuffer */
	if (rgb_input->fmt1 == 1)
		rgb_input->bpp = 4;
	else if (rgb_input->fmt0 == 1)
		rgb_input->bpp = 3;
	else
		rgb_input->bpp = 2;

	panel_info->size = fb_dev->panel_info->width * fb_dev->panel_info->height * rgb_input->bpp;

	x = fb_dev->panel_info->width;
	y = fb_dev->panel_info->height;

	while (x * y > 256)
	{
		if (x > y)
			x = (x + 1) / 2;
		else
			y = (y + 1) / 2;
	}
	fb_adj_line = (fb_dev->panel_info->height / y + 1) * y - fb_dev->panel_info->height;
	fb_dev->fb_adj_size = fb_adj_line * fb_dev->panel_info->width * rgb_input->bpp + 768;
	panel_info->dma_size = panel_info->size * (fb_dev->fb_type + 1) + fb_dev->fb_adj_size;

	panel_info->vaddr = dma_alloc_writecombine(
		dev,
		panel_info->dma_size,
		&panel_info->paddr,
		GFP_KERNEL);

	if (panel_info->vaddr == NULL)
	{
		dev_err(dev, "alloc fb memory error\n");
		ret = -ENOMEM;
		goto alloc_fb_err;
	}
	pr_err("framebuffer address: 0x%08x, size:%d\n",
		   panel_info->paddr, panel_info->dma_size);

	if (fb_shadow)
	{
		if (fb_shadow_dmasize > 0)
			panel_info->dma_size_shadow = fb_shadow_dmasize + fb_dev->fb_adj_size;
		else
			panel_info->dma_size_shadow = panel_info->size + fb_dev->fb_adj_size;

		panel_info->vaddr_shadow = dma_alloc_writecombine(
			dev,
			panel_info->dma_size_shadow,
			&panel_info->paddr_shadow,
			GFP_KERNEL);

		if (panel_info->vaddr_shadow == NULL)
		{
			dev_err(dev, "alloc shadow fb memory error\n");
			ret = -ENOMEM;
			goto alloc_fb_shadow_err;
		}
		pr_err("shadow framebuffer address: 0x%08x, size:%d\n",
			   panel_info->paddr_shadow, panel_info->dma_size_shadow);
	}

	/* copy logo file here */
	if (!fb_dev->lcd_ctrl_inited)
	{
		// memcpy(panel_info->vaddr, boot_logo, sizeof(boot_logo));
		ret = ak_load_logo_mtd(dev, panel_info->vaddr, panel_info->size * (fb_dev->fb_type + 1));
		if (ret)
		{
			dev_err(dev, "failed to load logo from mtd.\n");
		}
	}

	fb_dev->fb_id = 0;
	fb_dev->panel_info->paddr_offset = 0;

	/* set the display offset */
	fb_dev->rgb_input->h_offset = (fb_dev->panel_info->width - fb_dev->rgb_input->width) / 2;
	fb_dev->rgb_input->h_offset -= fb_dev->rgb_input->h_offset % 2;
	fb_dev->rgb_input->v_offset = (fb_dev->panel_info->height - fb_dev->rgb_input->height) / 2;
	fb_dev->rgb_input->v_offset -= fb_dev->rgb_input->v_offset % 2;

	/* set rgb display for logo display */
	if (!fb_dev->lcd_ctrl_inited)
	{
		if (fb_dev->panel_info->rgb_if == DISP_IF_MPU)
		{
			/* setting mpu direction. */
			mpu_display_dir(fb_dev, fb_dev->panel_info->panel_direction);
			/* setting mpu background color. */
			/*mpu_reflash_RGB_color(LCD_24BIT_RGB_COLOR_YELLOW,fb_dev);*/
		}
		/* display the logo. */
		ak_lcdc_rgb_display(fb_dev);
	}
#ifdef CONFIG_MACH_AK37E
	if (!fb_dev->lcd_ctrl_inited)
	{
		gpio_direction_output(fb_dev->backlight_pin, fb_dev->backlight_pin_active);
	}
#endif
	/* set default irq mask and enable lcd irq */
	if (fb_dev->panel_info->rgb_if != DISP_IF_MPU)
		ak_lcdc_set_irq_mask(fb_dev);

	if (devm_request_irq(&pdev->dev, fb_dev->irq, ak_fb_irq_handler, 0, pdev->name, fb_dev))
	{
		dev_err(&pdev->dev, "failed to request lcdc interrupt.\n");
		ret = -ENOENT;
		goto irq_err;
	}

	/* framebuffer */
	/* set fix info */
#if 0
	strncpy(fb->fix.id, "ak-lcd", sizeof(fb->fix.id));
	strncpy(fb_shadow->fix.id, "ak-lcd_shadow", sizeof(fb_shadow->fix.id));
	fb->fix.smem_start = panel_info->paddr;
	fb->fix.smem_len = panel_info->size*(fb_dev->fb_type + 1);
	fb->fix.type = FB_TYPE_PACKED_PIXELS;
	fb->fix.type_aux = 0;
	fb->fix.visual = FB_VISUAL_TRUECOLOR;
	fb->fix.xpanstep = 1;
	fb->fix.ypanstep = 1;
	fb->fix.ywrapstep = 0;
	fb->fix.line_length = panel_info->width * rgb_input->bpp;
	fb->fix.mmio_start = 0;
	fb->fix.mmio_len = 0;
	fb->fix.accel = FB_ACCEL_NONE;
	fb->fix.capabilities = FB_CAP_FOURCC;
	fb->fix.reserved[0] = fb_dev->fb_type;

	fb->screen_buffer = (char __iomem *)panel_info->vaddr;
	fb->screen_size = panel_info->size;
	if (fb->screen_base == NULL)
		pr_err("iomem address is null\n");
	
	/* set var info */
	fb->var.xres = fb_dev->rgb_input->width;
	fb->var.yres = fb_dev->rgb_input->height;
	fb->var.xres_virtual = fb_dev->panel_info->width;
	fb->var.yres_virtual = fb_dev->panel_info->height;
	fb->var.xoffset      = 0;
	fb->var.yoffset      = 0;
	fb->var.bits_per_pixel = rgb_input->bpp*8;
	fb->var.grayscale    = 0;
	fb->var.nonstd = 0;
	fb->var.activate = FB_ACTIVATE_NOW;
	fb->var.height = -1;
	fb->var.width = -1;
	
	fb->var.sync = 0;
	fb->var.vmode = FB_VMODE_NONINTERLACED;
	fb->var.rotate = 0;
	fb->var.reserved[0] = fb_dev->fb_id;

	ak_fb_set_bitfields(&fb->var, rgb_input->rgb_seq);

	fb->fbops = &fb_dev_ops;
	fb->flags = FBINFO_FLAG_DEFAULT | FBINFO_PARTIAL_PAN_OK | FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;
	/* used by .fb_fillrect and .fb_imgblt */
	fb->pseudo_palette = fb_dev->pseudo_palette;
#endif

	fill_fb(fb_dev, panel_info, rgb_input, fb, 0);
	if (fb_shadow)
		fill_fb(fb_dev, panel_info, rgb_input, fb_sd, 1);

	/* needed by FBIOGETCMAP & FBIOPUTCMAP */
	ret = fb_alloc_cmap(&fb->cmap, 256, 0);
	if (ret < 0)
	{
		dev_err(dev, "Failed to alloc cmap: %d\n", ret);
		goto alloc_cmap_err;
	}

	if (fb_shadow)
	{
		ret = fb_alloc_cmap(&fb_sd->cmap, 256, 0);
		if (ret < 0)
		{
			dev_err(dev, "Failed to alloc shadow fb cmap: %d\n", ret);
			goto alloc_fb_shadow_cmap_err;
		}
	}

	ret = register_framebuffer(fb);
	if (ret < 0)
	{
		dev_err(dev, "Failed to register framebuffer device: %d\n", ret);
		goto register_fb_err;
	}
	pr_err("fb%d: %s frame buffer init done\n", fb->node, fb->fix.id);

	if (fb_shadow)
	{
		ret = register_framebuffer(fb_sd);
		if (ret < 0)
		{
			dev_err(dev, "Failed to register shadow framebuffer device: %d\n", ret);
			goto register_fb_shadow_err;
		}
		pr_err("shadow fb%d: %s frame buffer init done\n", fb_sd->node, fb_sd->fix.id);
	}

	return 0;

register_fb_shadow_err:
	unregister_framebuffer(fb);
register_fb_err:
	if (fb_shadow)
		if (fb_sd->cmap.len)
			fb_dealloc_cmap(&fb_sd->cmap);
alloc_fb_shadow_cmap_err:
	if (fb->cmap.len)
		fb_dealloc_cmap(&fb->cmap);
alloc_cmap_err:
	free_irq(fb_dev->irq, fb_dev);
irq_err:
	if (fb_shadow)
	{
		dma_free_writecombine(
			dev,
			panel_info->dma_size_shadow,
			fb_dev->panel_info->vaddr_shadow,
			fb_dev->panel_info->paddr_shadow);
	}
alloc_fb_shadow_err:
	dma_free_writecombine(
		dev,
		panel_info->dma_size,
		fb_dev->panel_info->vaddr,
		fb_dev->panel_info->paddr);
alloc_fb_err:
	if (!fb_dev->lcd_ctrl_inited)
		clk_disable_unprepare(fb_dev->lcdc_clk);
resource_err:
	if (fb_shadow)
		framebuffer_release(fb_sd);
	framebuffer_release(fb);

	return ret;
}

static int ak_fb_remove(struct platform_device *pdev)
{
	struct ak_fb *fb_dev;
	struct fb_info *fb;
	struct fb_info *fb_sd;
	struct device *dev = &pdev->dev;
	u32 regval;

	fb_dev = platform_get_drvdata(pdev);
	fb = fb_dev->fb;
	if (fb_shadow)
		fb_sd = fb_dev->fb_shadow;

	if (fb_shadow)
		unregister_framebuffer(fb_sd);
	unregister_framebuffer(fb);

	if (fb_shadow)
		if (fb_sd->cmap.len)
			fb_dealloc_cmap(&fb_sd->cmap);

	if (fb->cmap.len)
		fb_dealloc_cmap(&fb->cmap);

	if (fb_shadow)
	{
		dma_free_writecombine(
			dev,
			fb_dev->panel_info->dma_size_shadow,
			fb_dev->panel_info->vaddr_shadow,
			fb_dev->panel_info->paddr_shadow);
	}

	dma_free_writecombine(
		dev,
		fb_dev->panel_info->dma_size,
		fb_dev->panel_info->vaddr,
		fb_dev->panel_info->paddr);

	if (!fb_dev->lcd_ctrl_inited)
		clk_disable_unprepare(fb_dev->lcdc_clk);

	/*
	 * reset lcd ctrl reg
	 */
	regval = __raw_readl(AK_LCD_SW_RESET_CTRL);
	regval |= (0x1 << 22);
	__raw_writel(regval, AK_LCD_SW_RESET_CTRL);
	mdelay(1);
	regval = __raw_readl(AK_LCD_SW_RESET_CTRL);
	regval &= ~(0x1 << 22);
	__raw_writel(regval, AK_LCD_SW_RESET_CTRL);
	mdelay(10);

	if (fb_dev->panel_info->rgb_if == DISP_IF_DSI)
	{
		if (!fb_dev->lcd_ctrl_inited)
			clk_disable_unprepare(fb_dev->dsi_clk);
	}

	if (fb_shadow)
		framebuffer_release(fb_sd);
	framebuffer_release(fb);

#ifdef CONFIG_MACH_AK37E
	gpio_direction_output(fb_dev->power_pin, !fb_dev->power_pin_active);
	pr_err("fb power pin %d,value %d\n", fb_dev->power_pin, gpio_get_value(fb_dev->power_pin));
#endif

	return 0;
}

#ifdef CONFIG_PM

static int ak_fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ak_fb *fb_dev;
	struct fb_info *fb;
	struct device *dev = &pdev->dev;
	u32 regval;
	int n;

	fb_dev = platform_get_drvdata(pdev);
	fb = fb_dev->fb;

	if (fb_dev->panel_info->rgb_if != DISP_IF_MPU)
	{
		// Close the LCD's IRQ
		__raw_writel(0, fb_dev->lcdc_reg + AK_LCD_INT_MASK);
	}

	// stop refresh the LCD panel
	ak_lcdc_pause(fb_dev);
	fb_dev->lcd_go = false;

	if (fb_dev->panel_info->rgb_if == DISP_IF_DSI)
	{
		// hold DSI DPHY Reset can make current smaller
		/* DSI DPHY Reset */
		regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
		regval &= ~(0x1 << 30);
		__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

		/*DSI Controller & DPHY clock gate disable */
		regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
		regval |= (0x1 << 31);
		__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

		// hold PCLK reset can make current smaller
		lcdc_set_reg(1, AK_LCD_PCLK_CTRL, 30, 30);
	}

	// disable LCD PCLK
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_PCLK);
	regval &= ~((1 << 8) | (1 << 9));
	regval |= (1 << 0);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_PCLK);

#ifdef CONFIG_MACH_AK37E
	gpio_direction_output(fb_dev->backlight_pin, !fb_dev->backlight_pin_active);
#endif

	if (fb_dev->panel_info->rgb_if == DISP_IF_MPU)
	{
		mpu_write_reg(LCD_MPU_CMD, 0x10, fb_dev); // enter Sleep Mode
		msleep(120);							  /* Delay 120ms, MPU timing need */
	}

	if (fb_dev->panel_info->rgb_if == DISP_IF_RGB)
	{
		/* disable rgb channel */
		regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
		regval &= ~(0x1 << 3);
		__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
	}

	if ((fb_dev->panel_info->rgb_if == DISP_IF_RGB) || (fb_dev->panel_info->rgb_if == DISP_IF_DSI))
	{
#ifdef CONFIG_MACH_AK37E
		msleep(20);
		gpio_direction_output(fb_dev->power_pin, !fb_dev->power_pin_active);
#endif
	}

	if (fb_dev->reset_pin >= 0)
	{
		gpio_direction_output(fb_dev->reset_pin, 0);
	}

	pinctrl_pm_select_sleep_state(&pdev->dev);

	dev_info(fb->dev, "lcd suspended\n");

	return 0;
}

static int ak_fb_resume(struct platform_device *pdev)
{
	struct ak_fb *fb_dev;
	struct fb_info *fb;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *child;
	char *name;
	u32 regval;
	u32 rgb_if;
	int try = 100;
	u32 irq_status;
	int n;

	fb_dev = platform_get_drvdata(pdev);
	fb = fb_dev->fb;

	pinctrl_pm_select_default_state(&pdev->dev);

	if (fb_dev->reset_pin >= 0)
	{
#ifdef CONFIG_MACH_AK37E
		gpio_direction_output(fb_dev->reset_pin, !fb_dev->reset_pin_active);
		mdelay(1);
		gpio_set_value(fb_dev->reset_pin, fb_dev->reset_pin_active);
		mdelay(1);
		gpio_set_value(fb_dev->reset_pin, !fb_dev->reset_pin_active);
		mdelay(1);
#else
		gpio_direction_output(fb_dev->reset_pin, 1);
		gpio_set_value(fb_dev->reset_pin, 1);
		mdelay(1);
		gpio_set_value(fb_dev->reset_pin, 0);
		mdelay(1);
		gpio_set_value(fb_dev->reset_pin, 1);
		mdelay(1);
#endif
	}

	if ((fb_dev->panel_info->rgb_if == DISP_IF_RGB) || (fb_dev->panel_info->rgb_if == DISP_IF_DSI))
	{
#ifdef CONFIG_MACH_AK37E
		gpio_direction_output(fb_dev->power_pin, fb_dev->power_pin_active);
		msleep(20);
#endif
	}

	if (fb_dev->panel_info->rgb_if == DISP_IF_DSI)
	{
		/*DSI Controller & DPHY clock gate enable */
		regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
		regval &= ~(0x1 << 31);
		__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

		/* DSI DPHY Reset */
		regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
		regval &= ~(0x1 << 30);
		__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

		/* DSI Controller TxByteClkHS, DSI Controller & dphy TxClkEsc reset */
		regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
		regval |= (0x3 << 28);
		__raw_writel(regval, AK_LCD_DSI_CLK_CFG);
		mdelay(1);
		regval = __raw_readl(AK_LCD_DSI_CLK_CFG);
		regval &= ~(0x3 << 28);
		__raw_writel(regval, AK_LCD_DSI_CLK_CFG);

		set_dsi_dphy_output_clock(fb_dev);
	}

	if (fb_dev->panel_info->rgb_if == DISP_IF_DSI)
	{
		for_each_available_child_of_node(node, child)
		{
			of_property_read_string(child, "compatible", (const char **)&name);
			of_property_read_u32(child, "panel-if", &rgb_if);
			if ((strcmp(name, fb_dev->panel_info->name) == 0) && (rgb_if == fb_dev->panel_info->rgb_if))
			{
				mipi_panel_cmd_setting(child, fb_dev);
			}
		}
	}

	// enable LCD PCLK
	regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_PCLK);
	regval |= (1 << 8) | (1 << 0);
	__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_PCLK);

	/* 维持PCLK域复位状态 */
	lcdc_set_reg(1, AK_LCD_PCLK_CTRL, 30, 30);

	// if ((fb_dev->panel_info->rgb_if != DISP_IF_RGB) && (fb_dev->panel_info->rgb_if != DISP_IF_DSI)) {
	if (fb_dev->panel_info->rgb_if != DISP_IF_RGB)
	{
		/* release维持PCLK域复位状态 */
		lcdc_set_reg(0, AK_LCD_PCLK_CTRL, 30, 30);
	}

	if (fb_dev->panel_info->rgb_if == DISP_IF_MPU)
	{
		for_each_available_child_of_node(node, child)
		{
			of_property_read_string(child, "compatible", (const char **)&name);
			of_property_read_u32(child, "panel-if", &rgb_if);
			if ((strcmp(name, fb_dev->panel_info->name) == 0) && (rgb_if == fb_dev->panel_info->rgb_if))
			{
				mpu_panel_cmd_setting(child, fb_dev);
			}
		}
	}

	if (fb_dev->panel_info->rgb_if == DISP_IF_RGB)
	{
		/* enable rgb channel */
		regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);
		regval |= (0x1 << 3);
		__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_TOP_CONFIG);

		/* set sw ctrl en */
		regval = __raw_readl(fb_dev->lcdc_reg + AK_LCD_SW_CTRL);
		regval |= (0x1 << 12);
		__raw_writel(regval, fb_dev->lcdc_reg + AK_LCD_SW_CTRL);

		/* release维持PCLK域复位状态*/
		lcdc_set_reg(0, AK_LCD_PCLK_CTRL, 30, 30);
	}

	ak_fb_go_enable(fb_dev);
	fb_dev->lcd_go = true;

	if (fb_dev->panel_info->rgb_if != DISP_IF_MPU)
	{
		while (try--)
		{
			irq_status = __raw_readl(fb_dev->lcdc_reg + AK_LCD_INT_STATUS);
			if (irq_status & IRQ_SYS_ERROR)
				continue;
			break;
		}
		ak_lcdc_set_irq_mask(fb_dev);
	}

#ifdef CONFIG_MACH_AK37E
	if ((fb_dev->panel_info->rgb_if == DISP_IF_RGB) || (fb_dev->panel_info->rgb_if == DISP_IF_DSI))
	{
		msleep(100); // skip few lcd frame
	}
	gpio_direction_output(fb_dev->backlight_pin, fb_dev->backlight_pin_active);
#endif

	dev_info(fb->dev, "lcd resume\n");

	return 0;
}

#else
#define ak_fb_suspend NULL
#define ak_fb_resume NULL
#endif

static const struct of_device_id ak_lcdc_match[] = {
	{
		.compatible = "anyka,ak37d-lcd-ctrl",
	},
	{
		.compatible = "anyka,ak37e-lcd-ctrl",
	},
	{}};
MODULE_DEVICE_TABLE(of, ak_lcdc_match);

static struct platform_driver ak_fb_driver = {
	.probe = ak_fb_probe,
	.remove = ak_fb_remove,
	.suspend = ak_fb_suspend,
	.resume = ak_fb_resume,
	.driver = {
		.owner = THIS_MODULE,
		.name = "ak-fb",
		.of_match_table = of_match_ptr(ak_lcdc_match),
	},
};

static int __init ak_fb_init(void)
{
	return platform_driver_register(&ak_fb_driver);
}

static void __exit ak_fb_exit(void)
{
	platform_driver_unregister(&ak_fb_driver);
}

subsys_initcall(ak_fb_init);
module_exit(ak_fb_exit);

MODULE_DESCRIPTION("Anyka LCD driver");
MODULE_AUTHOR("Anyka Microelectronic Ltd.");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.15");
