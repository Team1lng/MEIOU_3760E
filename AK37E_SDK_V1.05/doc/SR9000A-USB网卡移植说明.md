### 一.打开宏定义，编译驱动源码
####  例如配置文件：anycloud_ak37e_mini_defconfig
> #在“CONFIG_SLHC=m”下面加入打开以下配置文件\
> CONFIG_USB_NET_DRIVERS=m\
> CONFIG_USB_USBNET=m\
> CONFIG_USB_NET_CDCETHER=m

### 二.编译内核源码生成ko文件
> 路径在：kbd/drivers/net/需要复制三个文件到文件系统中：“mii.ko”，“/usb/usbnet.ko”，“usb/cdc_ether.ko”

### 三.加载ko文件
依次按照顺序加载ko文件

> modprobe cfg80211\
> modprobe usb-common\
> modprobe usbcore\
> insmod /usr/modules/ak_hcd.ko \
> insmod /usr/modules/mii.ko \
> insmod /usr/modules/usbnet.ko 
> insmod /usr/modules/cdc_ether.ko 
#### 打印日志
>> cdc_ether 1-1:2.0 eth2: register 'cdc_ether' at usb-Anyka usb host controller-1, CDC Ethernet Device, 00:e0:99:4f:c3:03
usbcore: registered new interface driver cdc_ether
### 四.获取IP地址
>> udhcpc -i eth2\
>> ping www.baidu.com

注意:如果USB口与烧录口公用，不要插入烧录先，根据硬件清空具体分析
注意:如果是有两线phy的网桥设备，需要在“insmod ak_eth.ko”之后才能执行步骤三，主要选择网桥的网卡设备需正确。不要将两线phy与usb的phy搭了网桥，以免出现问题。
