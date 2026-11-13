
#!/bin/bash
# 脚本用途：根据 config.mk 的配置，组装根文件系统、驱动、脚本并生成烧录镜像。
# 使用方式：./make_image.sh <SAT_OS_CHECK> <OS_VERSION>
# 参数说明：
#   SAT_OS_CHECK - 用于在 rootfs/app/data 中创建标识文件（例如区分 SAT 系统）
#   OS_VERSION   - 可选的系统版本标识（当前脚本中主要从 config.mk 读取实际配置）

SAT_OS_CHECK=$1
OS_VERSION=$2
echo "SAT_OS_CHECK: $SAT_OS_CHECK"
echo "OS_VERSION: $OS_VERSION"

# 设置常用目录变量（基于当前工作目录）
image_install_dir=`pwd`/image/
platform_dir=`pwd`/platform/
upgrade_dir=`pwd`/upgrade/platform/

# 从 config.mk 中读取配置信息的函数（FLASH_TYPE, SCREEN_TYPE, BOARD_TYPE, CHIP_TYPE, WIFI_TYPE）
get_config_info()
{
    FLASH_TYPE=`awk -F= '/^FLASH_TYPE/{gsub(" ","",$2); printf $2}' config.mk`
    SCREEN_TYPE=`awk -F= '/^SCREEN_TYPE/{gsub(" ","",$2); printf $2}' config.mk`
    BOARD_TYPE=`awk -F= '/^BOARD_TYPE/{gsub(" ","",$2); printf $2}' config.mk`
    CHIP_TYPE=`awk -F= '/^CHIP_TYPE/{gsub(" ","",$2); printf $2}' config.mk`
    # 下面注释的分支可以按需要启用基于 OS_VERSION 的 WIFI_TYPE 设定
    # if [ "$OS_VERSION" = "SAT" ]; then
    #     WIFI_TYPE=hi3881
    # elif [ "$OS_VERSION" = "WS73V100" ]; then
    #     WIFI_TYPE=ws73v100
    # else
    #     WIFI_TYPE=hi3881
    # fi
    WIFI_TYPE=`awk -F= '/^WIFI_TYPE/{gsub(" ","",$2); printf $2}' config.mk`

    echo "FLASH_TYPE="$FLASH_TYPE", BOARD_TYPE="$BOARD_TYPE", CHIP_TYPE="$CHIP_TYPE ", WIFI_TYPE="$WIFI_TYPE
}

# 解压根文件系统（调用同目录下的 extract.sh）
extract_fs()
{
    # 解压缩根文件系统
    ./extract.sh

}

# 处理 wifi 驱动：根据 WIFI_TYPE 将对应的驱动复制到 rootfs/usr/modules
process_wifi_driver()
{
    echo "WIFI_TYPE="$WIFI_TYPE
    # 根据选择的WIFI类型，拷贝对应的模块到 rootfs
    if test $WIFI_TYPE = "NO_WIFI" ; then
        # 无无线
        true
    elif test $WIFI_TYPE = "rtl8188ftv" ; then
        cp -rf ko/external/rtl8188ftv.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "rtl8189ftv" ; then
        cp -rf ko/external/rtl8189ftv.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "atbm6031" ; then
        cp -rf ko/external/atbm6031/atbm6031.ko rootfs/usr/modules/
        cp -rf ko/external/atbm6031/ak_mci.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "atbm6032" ; then
        cp -rf ko/external/atbm6032.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "ssv6x5x" ; then
        cp -rf ko/external/ssv6x5x.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "hi3881" ; then
        cp -rf ko/external/hi3881/hi3881.ko rootfs/usr/modules/
        cp -rf ko/external/hi3881/ak_mci.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "ws73v100" ; then
        cp -rf ko/external/ws73v100/plat_soc.ko rootfs/usr/modules/
        cp -rf ko/external/ws73v100/wifi_soc.ko rootfs/usr/modules/
        cp -rf ko/external/ws73v100/ak_mci.ko rootfs/usr/modules/
    elif test $WIFI_TYPE = "rtl8731ftv" ; then   
        cp -rf ko/external/rtl8731ftv.ko rootfs/usr/modules/
    fi
    echo "WIFI_TYPE="$WIFI_TYPE
}

# 处理屏幕驱动：根据 SCREEN_TYPE 拷贝对应驱动或脚本
process_screen_driver()
{
    if test -z $SCREEN_TYPE ; then
        # 未设置屏幕类型则直接返回
        return
    fi

    if test $SCREEN_TYPE = "NO_SCREEN" ; then
        # 无屏幕
        true
    elif test $SCREEN_TYPE = "RGB" ; then
        # RGB 屏幕相关触摸驱动
        cp -rf ko/external/ts_icn85xx.ko rootfs/usr/modules/
        true
    elif test $SCREEN_TYPE = "MIPI" ; then
        # MIPI 屏幕：当前注释掉了具体驱动拷贝
        # cp -rf ko/external/ts_icn85xx_mipi.ko rootfs/usr/modules/
        true
    else
        echo "Screen Type Error, please check!"
        exit 1
    fi
}

# 下列为传感器相关的模板/注释代码块（目前被注释，保留作参考）
#process_sensor_driver()
#{
    #cp -rf ko/external/sensor_*.ko rootfs/usr/modules/
    #cp -rf ko/external/sensor_gc0308.ko rootfs/usr/modules/
    #cp -rf ko/external/sensor_tp9950.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_ar0230.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_pr2000.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_sc4236.ko rootfs/usr/modules/
#    if test $SENSOR_TYPE = "NO_SENSOR" ; then
#       true
#   elif test $SENSOR_TYPE = "gc4653" ; then
#       cp scripts/sensor/gc4653.sh rootfs/usr/sbin/sensor.sh
#   elif test $SENSOR_TYPE = "pr2000" ; then
#       cp scripts/sensor/pr2000.sh rootfs/usr/sbin/sensor.sh
#   elif test $SENSOR_TYPE = "ar0230" ; then
#       cp scripts/sensor/ar0230.sh rootfs/usr/sbin/sensor.sh
#   elif test $SENSOR_TYPE = "sc4236" ; then
#       cp scripts/sensor/sc4236.sh rootfs/usr/sbin/sensor.sh
#   else
#       echo "Sensor Type Error, please check!"
#       exit 1
#   fi
#}

# 安装驱动到文件系统：清理旧模块，拷贝通用驱动并调用 wifi、screen 处理函数
install_driver()
{
    if test -d rootfs/usr/modules ; then
        # 删除文件系统中遗留的驱动文件
        rm -rf rootfs/usr/modules/*
        rm -rf rootfs/lib/modules/*
    else
        # 若目录不存在则创建
        mkdir rootfs/usr/modules
    fi

    # 拷贝新的驱动到文件系统（列出了常用的一些驱动模块）
    #cp -rf ko/external/ak_gpio_keys.ko rootfs/usr/modules/
   cp -rf ko/external/ak_saradc.ko rootfs/usr/modules/
   cp -rf ko/external/goodix.ko rootfs/usr/modules/
    #cp -rf ko/external/ak_adc_key.ko rootfs/usr/modules/
#    cp -rf ko/external/ak_crypto.ko rootfs/usr/modules/
    cp -rf ko/external/ak_efuse.ko rootfs/usr/modules/
    cp -rf ko/external/ak_eth.ko rootfs/usr/modules/
    cp -rf ko/external/ak_hcd.ko rootfs/usr/modules/
    cp -rf ko/external/ak_i2c.ko rootfs/usr/modules/
    cp -rf ko/external/ak_ion.ko rootfs/usr/modules/
#    cp -rf ko/external/ak_isp.ko rootfs/usr/modules/
        cp -rf ko/external/ak_image_capture.ko rootfs/usr/modules/
    #cp -rf ko/external/ak_leds.ko rootfs/usr/modules/
    cp -rf ko/external/ak_mci.ko rootfs/usr/modules/
    #cp -rf ko/external/ak_motor.ko rootfs/usr/modules/
    cp -rf ko/external/ak_pcm.ko rootfs/usr/modules/
    cp -rf ko/external/ak_pwm_char.ko rootfs/usr/modules/
    cp -rf ko/external/ak_ramdisk.ko rootfs/usr/modules/
    cp -rf ko/external/ak_rtc.ko rootfs/usr/modules/
    #cp -rf ko/external/ak_saradc.ko rootfs/usr/modules/
    cp -rf ko/external/ak_udc.ko rootfs/usr/modules/
    cp -rf ko/external/ak_uio.ko rootfs/usr/modules/
    cp -rf ko/external/ak_fb.ko rootfs/usr/modules/
    cp -rf ko/external/ak_gui.ko rootfs/usr/modules/
    cp -rf ko/external/ak_cs42l52.ko rootfs/usr/modules/
#    cp -rf ko/external/cryptodev.ko rootfs/usr/modules/
    cp -rf ko/external/exfat.ko rootfs/usr/modules/
    cp -rf ko/external/ts_gsl.ko rootfs/usr/modules/
    cp -rf ko/external/ts_ft5446u.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_ar0230.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_f22_f23_f28_f35_f37.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_gc2063.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_gc4653.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_pr2000.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_sc2145.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_sc2335.ko rootfs/usr/modules/
#    cp -rf ko/external/sensor_sc4236.ko rootfs/usr/modules/

    cp -rf ko/external/mii.ko rootfs/usr/modules/
    cp -rf ko/external/usbnet.ko rootfs/usr/modules/
    cp -rf ko/external/cdc_ether.ko rootfs/usr/modules/
    
    # cp -rf ko/external/hi3881.ko rootfs/usr/modules/
    
    # 将内部库拷贝到 rootfs（internal/lib -> rootfs/lib）
    cp -rf ko/internal/lib rootfs/
    # 删除一些不需要的内核模块路径下的文件（清理冗余）
    rm -rf rootfs/lib/modules/4.4.*/kernel/crypto/
    rm -rf rootfs/lib/modules/4.4.*/kernel/drivers/staging/rtl8188eu
    rm -rf rootfs/lib/modules/4.4.*/kernel/net/wireless/mac80211.ko

    # 处理按配置选择的 wifi 与 screen 驱动
    process_wifi_driver

    process_screen_driver

    #process_sensor_driver
}

# 安装平台中间件的库文件到文件系统（将 platform/lib/*.so 拷贝到 rootfs 的 usr/lib）
install_platform()
{
    # 设置库文件的安装目录（rootfs/rootfs 是提取的实际根目录）
    rootfs_dir=`pwd`/rootfs/rootfs
    lib_install_dir=$rootfs_dir/usr/lib
    sample_install_dir=$rootfs_dir/usr/bin
    cp -rf $platform_dir/lib/*.so $lib_install_dir
    cd platform/sample
    # 如果需要可以在此构建 sample 并安装
    #make 
    #make install SAMPLE_INSTALL_DIR=$sample_install_dir
    cd -
}

# 构建驱动示例（通常在 os/driver/external/sample 下）
build_drv_sample()
{
    # 设置库文件的安装目录
    cd os/driver/external/sample
    #make 
    #make install INSTALL=$sample_install_dir
    cd -
}

# 根据 FLASH_TYPE 选择并复制相应的启动脚本与更新脚本（NOR/NAND）
process_flash_scripts()
{
    if test $FLASH_TYPE = "NOR" ; then
        cp ./scripts/flash/rc.local.nor rootfs/etc/init.d/rc.local
        cp ./scripts/flash/update.nor.sh rootfs/sbin/update.sh
    elif test $FLASH_TYPE = "NAND" ; then
        cp ./scripts/flash/rc.local.nand rootfs/etc/init.d/rc.local
        cp ./scripts/flash/update.nand.sh rootfs/sbin/update.sh
        cp ./scripts/flash/nand_update rootfs/bin/nand_update
    else
        echo "Flash type error, please check!"
        exit 1
    fi
}

# 传感器相关脚本的注释模板（保留）
#process_sensor_scripts()
#{
    #cp scripts/sensor/*.sh rootfs/usr/sbin/
    #cp scripts/sensor/gc0308.sh rootfs/usr/sbin/
    #cp scripts/sensor/tp9950.sh rootfs/usr/sbin/
#    cp scripts/sensor/pr2000.sh rootfs/usr/sbin/
#    cp scripts/sensor/ar0230.sh rootfs/usr/sbin/
#    cp scripts/sensor/sc4236.sh rootfs/usr/sbin/
#   if test $SENSOR_TYPE = "NO_SENSOR" ; then
#       true
#   elif test $SENSOR_TYPE = "gc4653" ; then
#       cp scripts/sensor/gc4653.sh rootfs/usr/sbin/sensor.sh
#   elif test $SENSOR_TYPE = "pr2000" ; then
#       cp scripts/sensor/pr2000.sh rootfs/usr/sbin/sensor.sh
#   elif test $SENSOR_TYPE = "ar0230" ; then
#       cp scripts/sensor/ar0230.sh rootfs/usr/sbin/sensor.sh
#   elif test $SENSOR_TYPE = "sc4236" ; then
#       cp scripts/sensor/sc4236.sh rootfs/usr/sbin/sensor.sh
#   else
#       echo "Sensor Type Error, please check!"
#       exit 1
#   fi
#}

# 处理屏幕相关启动/配置脚本，拷贝到 rootfs/usr/sbin
process_screen_scripts()
{
    if test -z $SCREEN_TYPE ; then
        return
    fi

    if test $SCREEN_TYPE = "NO_SCREEN" ; then
        true
    elif test $SCREEN_TYPE = "RGB" ; then
        cp scripts/screen/screen.sh rootfs/usr/sbin/
    elif test $SCREEN_TYPE = "MIPI" ; then
        cp scripts/screen/screen_mipi.sh rootfs/usr/sbin/
    else
        echo "Screen Type Error, please check!"
        exit 1
    fi
}

# 处理 wifi 相关脚本与文件，拷贝到 rootfs 并设置 wifi_driver.sh
process_wifi_scripts()
{
    # 根据选择的WIFI类型，拷贝对应的脚本及 wifi 目录下的资源
    if test $WIFI_TYPE = "NO_WIFI" ; then
        true
    elif test $WIFI_TYPE = "rtl8188ftv" ; then
        cp scripts/wifi/rtl8188.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "rtl8189ftv" ; then
        cp scripts/wifi/rtl8189.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "atbm6031" ; then
        cp scripts/wifi/atbm6031.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "atbm6032" ; then
        cp scripts/wifi/atbm6032.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "ssv6x5x" ; then
        cp scripts/wifi/ssv6x5x.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "hi3881" ; then
        cp scripts/wifi/hi3881.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "ws73v100" ; then
        cp scripts/wifi/ws73.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    elif test $WIFI_TYPE = "rtl8731ftv" ; then    
        cp scripts/wifi/rtl8731ftv.sh rootfs/usr/sbin/wifi_driver.sh
        cp -rf ./wifi/* rootfs/
    else
        echo "Wifi Type Error, please check!"
        exit 1
    fi
}

# 拷贝文件系统相关通用脚本到 rootfs/usr/sbin
process_fs_scripts()
{
    cp -rf scripts/fs/* rootfs/usr/sbin/
}

# 拷贝其它工具脚本到 rootfs/usr/sbin
process_other_scripts()
{
    cp -rf scripts/others/* rootfs/usr/sbin/
}

# 安装各类脚本（flash/screen/wifi/fs/others）到 rootfs，并拷贝 utils
install_scripts()
{
    process_flash_scripts

    process_screen_scripts

    #process_sensor_scripts

    process_wifi_scripts

    process_fs_scripts

    process_other_scripts
    UTILS_SUPPORT=1

    if test $UTILS_SUPPORT -eq 1 ; then
        cp -rf utils/* rootfs/
    fi

}

# 制作烧录镜像：根据 FLASH_TYPE 选择 jffs2/squashfs 或 yaffs2，并生成镜像文件放 image_install_dir
make_image()
{
    # 清理并创建必要的数据目录与标识文件
    # rm -rf rootfs/app/data/*
    # mkdir rootfs/app/data/tuya_key
    # touch rootfs/app/data/$SAT_OS_CHECK

    if test $FLASH_TYPE = "NOR" ; then
        # 针对 NOR flash：生成 jffs2 与 squashfs 镜像
        ./create_jffs2fs.sh
        ./create_squashfs.sh

        # 将制作好的镜像文件复制到 image_install_dir
        cp -rf usr.sqsh4 $image_install_dir
        cp -rf config.jffs2 $image_install_dir
        cp -rf root.sqsh4 $image_install_dir
        cp -rf app.sqsh4 $image_install_dir
        cp -rf data.jffs2 $image_install_dir
    elif test $FLASH_TYPE = "NAND" ; then
        # 针对 NAND flash：生成 yaffs2 镜像
        ./create_yaffs2fs.sh
        cp -rf *.yaffs2 $image_install_dir
    fi

}

# 将生成的镜像复制到 upgrade/platform 目录，供升级或烧录工具使用
copy_image()
{
    # 将制作好的烧录镜像放到 upgrade/platform 下
    cp -rf $image_install_dir/* $upgrade_dir
}



# 执行流程：读取配置 -> 进入 rootfs -> （可解压） -> 安装平台库 -> 构建示例 -> 安装驱动与脚本 -> 生成镜像 -> 复制镜像
get_config_info

cd rootfs

# extract_fs   # 如果需要先解压 rootfs，可取消注释

cd ..

install_platform

build_drv_sample

cd rootfs

install_driver

install_scripts

make_image

cd ..

copy_image
# ...existing code...