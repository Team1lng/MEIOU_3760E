#!/bin/sh

function get_partition_dev()
{
	mtdblockid=`cat /proc/mtd| grep "$1"| grep -E -o "mtd[0-9]+" | grep -E -o "[0-9]+"`
	if [ "$mtdblockid" != "" ];then
		echo "/dev/mtdblock$mtdblockid"
	else
		echo ""
	fi
}

#print kernel error default
echo 4 > /proc/sys/kernel/printk

#start ftp server, dir=root r/w, -t 600s(timeout)
/usr/bin/tcpsvd 0 21 ftpd -w / -t 600 &

#echo "start telnet......"
#/usr/sbin/telnetd &


echo "----------------------SAT---------------------------------" 

#start syslogd & klogd, log rotated 3 files(200KB) to /var/log/messages
syslogd -D -n -O /var/log/messages -s 200 -b 3 & # -l prio
klogd -n & # -c prio

ifconfig lo 127.0.0.1


dmesg > /tmp/start_message

## set min free reserve bytes
echo 2048 > /proc/sys/vm/min_free_kbytes

echo "/tmp/core_%e_%p_%t" > /proc/sys/kernel/core_pattern

#insmod /usr/modules/ak_adc_key.ko
insmod /usr/modules/ak_rtc.ko
hwclock -s

#insmod /usr/modules/ak_fb.ko lcd_ctl_force_init=1
insmod /usr/modules/ak_pwm_char.ko
# insmod /usr/modules/ak_fb.ko
# insmod /usr/modules/ak_saradc.ko
insmod /usr/modules/ak_i2c.ko
insmod /usr/modules/ak_pcm.ko
#insmod /usr/modules/ak_gpio_keys.ko
#insmod /usr/modules/ak_gui.ko
insmod /usr/modules/ak_ion.ko
#insmod /usr/modules/ak_leds.ko
#insmod /lib/modules/4.4.192V1.5/kernel/drivers/mmc/core/mmc_core.ko
#insmod /lib/modules/4.4.192V1.5/kernel/drivers/mmc/card/mmc_block.ko
#insmod /usr/modules/ak_mci.ko
insmod /usr/modules/ak_uio.ko
# insmod /usr/modules/ts_gsl.ko
# insmod /usr/modules/ts_ft5446u.ko
#insmod /usr/modules/exfat.ko

modprobe mmc_core.ko
modprobe mmc_block.ko
insmod /usr/modules/ak_mci.ko


# insmod /usr/modules/rtl8189ftv.ko


#安装fb驱动，检查lcd屏参，加载触摸驱动
insmod /usr/modules/ak_fb.ko
insmod /usr/modules/ak_gui.ko
lcd_panel=$(cat /sys/module/ak_fb/parameters/panel_compatible 2>/dev/null)
echo "$lcd_panel"
if [ "$lcd_panel" = "adt,adt07029bm30-10_IPS" ]; then
    # echo "lcd_panel节点屏参为adt,adt07029bm30-10_IPS,使用gt911模块!!!!!"
	# insmod /usr/modules/goodix.ko 2>/dev/null

	echo "gslX680节点状态为okay,插入gslX680.ko模块!!!!!"
	insmod /usr/modules/ts_gsl.ko 2>/dev/null
else
	# 检查i2c节点子节点ts_ft5446u的状态
	ft5446u=$(cat /sys/firmware/devicetree/base/soc/i2c2@201c0000/touch-screen@38/status 2>/dev/null)
	if [ "$ft5446u" = "okay" ]; then
		echo "ts_ft5446u节点状态为okay,插入ts_ft5446uko模块!!!!!"
		insmod /usr/modules/ts_ft5446u.ko
	else
		gslX680=$(cat /sys/firmware/devicetree/base/soc/i2c2@201c0000/touch-screen@40/status 2>/dev/null)
		if [ "$gslX680" = "okay" ]; then
		echo "gslX680节点状态为okay,插入gslX680.ko模块!!!!!"
		insmod /usr/modules/ts_gsl.ko 2>/dev/null
		else
    	echo "未找到对应触摸驱动！！！！"
		fi
	fi
fi

# 检查saradc节点状态
saradc=$(cat /proc/device-tree/soc/saradc@08000000/status)
if [ "$saradc" = "okay" ]; then
    echo "检查saradc节点状态节点状态为okay,插入ak_saradc模块"
	insmod /usr/modules/ak_saradc.ko
fi

# start network
insmod /usr/modules/ak_eth.ko yt8510_rate_model=0x01
ifconfig eth0 up
if test ! -f "/etc/config/mac.txt" ; then
cat /sys/class/net/eth0/address > /etc/config/mac.txt
else
cat /etc/config/mac.txt | xargs -i /sbin/ifconfig eth0 hw ether {}
fi

# modprobe cfg80211
echo 26 > /sys/class/gpio/export
status=$(cat /sys/class/gpio/gpio26/value)
# if [ "$status" = "1" ]; then
#     echo "检测非SE版本,插入WIFI、USB模块"
# 	modprobe cfg80211.ko
	
# 	modprobe usb-common
# 	modprobe usbcore
# 	insmod /usr/modules/ak_hcd.ko
# 	insmod /usr/modules/mii.ko
# 	
# 	insmod /usr/modules/cdc_ether.ko

# 	if test  -f "/usr/modules/wifi_soc.ko" ; then
# 		# wifi_driver.sh sta &
# 		insmod /usr/modules/plat_soc.ko
# 		insmod /usr/modules/wifi_soc.ko
# 	else
# 		echo 83 > /sys/class/gpio/export
# 		echo out > sys/class/gpio/gpio83/direction
# 		echo 0 > sys/class/gpio/gpio83/value
# 		if [ "$ft5446u" = "okay" ]; then
# 			 insmod /usr/modules/rtl8731ftv.ko
# 			# insmod /usr/modules/hi3881.ko
# 		else
# 			insmod /usr/modules/rtl8731ftv.ko
# 			# insmod /usr/modules/hi3881.ko
# 		fi
# 	fi

 # 插入3881wifi模块
if [ "$status" = "1" ]; then
    echo "检测非SE版本,插入WIFI、USB模块"
	modprobe cfg80211.ko
	if test  -f "/usr/modules/wifi_soc.ko" ; then
		# wifi_driver.sh sta &
		insmod /usr/modules/plat_soc.ko
		insmod /usr/modules/wifi_soc.ko
	else
		if [ "$ft5446u" = "okay" ]; then
			insmod /usr/modules/hi3881.ko
		else
			insmod /usr/modules/hi3881.ko
		fi
	fi

	modprobe usb-common
	modprobe usbcore
	insmod /usr/modules/ak_hcd.ko
	insmod /usr/modules/mii.ko
	insmod /usr/modules/usbnet.ko
	insmod /usr/modules/cdc_ether.ko
	sleep 1
	wpa_supplicant -Dnl80211 -i wlan0 -c /etc/config/wpa_supplicant.conf -B &
	udhcpc -i wlan0 &
	sleep 1
else
    echo "检测SE版本,不插入WIFI、USB模块"
fi
# mount yaffs2 file-system.
# mount -t yaffs2 /dev/mtdblock9 /data
# partition_data=`get_partition_dev DATA`
# if [ "$partition_data" != "" ];then
# 	echo "/bin/mount -t yaffs2 $partition_data /data"
# 	/bin/mount -t yaffs2 $partition_data /data
# fi


#echo "--------------------------PATH------------------1"
#echo $LD_LIBRARY_PATH
#echo $PATH
#echo $TSLIB_TSDEVICE
#echo "--------------------------PATH------------------2"

LD_LIBRARY_PATH=/lib:/usr/lib
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH PATH

export QWS_MOUSE_PROTO=tslib:/dev/input/event0
export TSLIB_TSDEVICE=/dev/input/event0
export TSLIB_TSEVEVTTYPE=input
export TSLIB_CONFFILE=/etc/ts.conf
export TSLIB_PLUGINDIR=/usr/lib/ts
export TSLIB_CALIBFILE=/etc/pointercal
export TSLIB_CONSOLEDEVICE=none
export TSLIB_FBDEVICE=/dev/fb0
#echo "--------------------------PATH-----------------3"
#echo $LD_LIBRARY_PATH
#echo $PATH
#echo $TSLIB_TSDEVICE
#echo "--------------------------PATH------------------4"

#start system service
#/usr/sbin/service.sh start &


# /usr/sbin/screen.sh

if [ ! -e /etc/config/DisableApp ]; then
	echo "DisableApp file no exists, not start app.sh"


	cp /etc/config/app.sh /tmp
	/tmp/app.sh &
fi
