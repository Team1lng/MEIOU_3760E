/*
 * AK37D lcd driver
 *
 * Copyright (C) 2018 Anyka(Guangzhou) Microelectronics Technology Co., Ltd.
 *
 * Author: Feilong Dong <dong_feilong@anyka.com>
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

#ifndef _AK37D_LCD_REGS_H_
#define _AK37D_LCD_REGS_H_

// CIS SCLK  & LCD PCLK CFG REGISTER
#define AK_PERI_PLL   	(AK_VA_SYSCTRL + 0x0014)
#define AK_SHAREPIN   	(AK_VA_SYSCTRL + 0x0188)

#define AK_LCD_CLOCK_GATE   	(AK_VA_SYSCTRL + 0x001C)
#define AK_LCD_SW_RESET_CTRL	(AK_VA_SYSCTRL + 0x0020)
#define AK_LCD_PCLK_CTRL		(AK_VA_SYSCTRL + 0x0100)
#define AK_LCD_DSI_CLK_CFG		(AK_VA_SYSCTRL + 0x0074)
#define AK_LCD_DSI_LANE_SWAP	(AK_VA_SYSCTRL + 0x0078)
#define AK_CLK_GATE_AND_SOFT_RST	(AK_VA_SYSCTRL + 0x00FC)
#define AK_CLK_SOFT_RST			(AK_VA_SYSCTRL + 0x0020)

/* LCD controller registers */
#define AK_LCD_TOP_CONFIG			(0x00)  //LCD Top config
#define AK_LCD_MPU_1				(0x04)  //LCD MPU 1
#define AK_MPU_READ					(0x0C)  //MPU read
#define AK_LCD_RGB_GEN_INFO			(0x10)  //RGB general info cfg
#define AK_LCD_RGB_CTRL				(0x14)  //RGB channel ctrl
#define AK_RGB_VIRTUAL_LEN			(0x18)  //RGB virtual len
#define AK_RGB_VIRTUAL_OFFSET		(0x1C)  //RGB virtual offset
#define AK_OSD_ADDRESS				(0x20)  //OSD address
#define AK_OSD_OFFSET				(0x24)  //OSD offset
#define AK_OSD_PALETTE_ADD			(0x28)  //OSD palette add
#define AK_OSD_SIZE_ALPHA			(0x38)  //OSD size alpha
#define AK_RGB_BACKGROUND			(0x3C)  //RGB background
#define AK_RGB_INTERFACE1			(0x40)  //RGB interface 1
#define AK_RGB_INTERFACE2			(0x44)  //RGB interface 2
#define AK_RGB_INTERFACE3			(0x48)  //RGB interface 3
#define AK_RGB_INTERFACE4			(0x4C)  //RGB interface 4
#define AK_RGB_INTERFACE5			(0x50)  //RGB interface 5
#define AK_RGB_INTERFACE6			(0x54)  //RGB interface 6
#define AK_RGB_INTERFACE7			(0x58)  //RGB interface 7
#define AK_RGB_OFFSET				(0x9C)  //RGB offset setting
#define AK_RGB_SIZE					(0xA0)  //RGB size
#define AK_PANEL_SIZE				(0xA4)  //PANEL size
#define AK_REG_CONFIGURE			(0xA8)  //REG configure
#define AK_LCD_GO					(0xAC)  //LCD go
#define AK_LCD_INT_STATUS			(0xB0)  //LCD status
#define AK_LCD_INT_MASK				(0xB4)  //LCD int mask
#define AK_LCD_SW_CTRL				(0xB8)  //LCD sw ctrl
#define AK_LCD_PCLK					(0xBC)  //LCD pclk

#define AK_PACKET_HEAD_REG			(0xD0)  //Packet head register
#define AK_TX_PAYLOAD_REG			(0xD4)  //Tx_payload register
#define AK_DPI_STATUS_REG			(0xD8)  //status register
#define AK_DPI_CONFIG_REG			(0xDC)  //configuration register

/* DSI(Display Serial Interface) config for MIPI LCD */
#define AK_DSI_NUM_LANES			(0x00)  //CFG_NUM_LANES register
#define AK_DSI_NONCONTINUOUS_CLK	(0x04)  //CFG_NONCONTINUOUS_CLK
#define AK_DSI_T_PRE				(0x08)  //CFG_T_PRE register
#define AK_DSI_T_POST				(0x0C)  //CFG_T_POST register
#define AK_DSI_TX_GAP				(0x10)  //CFG_TX_GAP register
#define AK_DSI_AUTOINSERT_EOTP		(0x14)  //CFG_AUTOINSERT_EOTP register
#define AK_DSI_EXT_CMDS_AFTER_EOTP	(0x18)  //CFG_EXTRA_CMDS_AFTER_EOTP
#define AK_DSI_HTX_TO_COUNT			(0x1C)  //CFG_HTX_TO_COUNT register
#define AK_DSI_LRX_H_TO_COUNT		(0x20)  //CFG_LRX_H_TO_COUNT register
#define AK_DSI_BTA_H_TO_COUNT		(0x24)  //CFG_BTA_H_TO_COUNT register
#define AK_DSI_T_WAKEUP				(0x28)  //CFG_TWAKEUP register
#define AK_DSI_STATUS_OUT			(0x2C)  //CFG_STATUS_OUT register
#define AK_DSI_PIX_PAYPLOAD_SIZE	(0x200)  //CFG_DPI_PIXEL_PAYLOAD_SIZE register
#define AK_DSI_PIX_FIFO_SEND_LEVEL	(0x204)  //CFG_DPI_PIXEL_FIFO_SEND_LEVEL register
#define AK_DSI_IF_COLOR_CODING		(0x208)  //CFG_DPI_PIXEL_COLOR_CODING register
#define AK_DSI_PIX_FORMAT			(0x20C)  //CFG_DPI_PIXEL_FORMAT register
#define AK_DSI_VSYNC_POL			(0x210)  //CFG_DPI_VSYNC_POLARITY register
#define AK_DSI_HSYNC_POL			(0x214)  //CFG_DPI_HSYNC_POLARITY register
#define AK_DSI_VIDEO_MODE			(0x218)  //CFG_DPI_VIDEO_MODE register
#define AK_DSI_HFP					(0x21C)  //CFG_DPI_HFP register
#define AK_DSI_HBP					(0x220)  //CFG_DPI_HBP register
#define AK_DSI_HSA					(0x224)  //CFG_DPI_HSA register
#define AK_DSI_MULT_PKTS_EN			(0x228)  //CFG_DPI_ENABLE_MULT_PKTS register
#define AK_DSI_VBP					(0x22C)  //CFG_DPI_VBP register
#define AK_DSI_VFP					(0x230)  //CFG_DPI_VFP register
#define AK_DSI_BLLP_MODE			(0x234)  //CFG_DPI_BLLP_MODE register
#define AK_DSI_USE_NULL_PKT_BLLP	(0x238)  //CFG_DPI_USE_NULL_PKT_BLLP register
#define AK_DSI_VACTIVE				(0x23C)  //CFG_DPI_VACTIVE register
#define AK_DSI_VC					(0x240)  //CFG_DPI_VC register

#endif	/* _AK37D_LCD_REGS_H */
