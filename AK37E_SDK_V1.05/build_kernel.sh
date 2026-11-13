#/bin/sh
###
 # @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @Date: 2024-01-31 15:52:33
 # @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @LastEditTime: 2024-07-16 14:57:08
 # @FilePath: /AK37E_SDK_V1.05/build_kernel.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 

get_toolchain()
{
	CURR_PATH=$(pwd)
	CROSS_COMPILE=`awk -F= '/^CROSS_COMPILE/{gsub(" ","",$2); printf $2}' ./rules.mk`
    echo "CROSS_COMPILE : $CROSS_COMPILE"
	CROSS_COMPILE=$CURR_PATH/$CROSS_COMPILE
	echo "CROSS_COMPILE : $CROSS_COMPILE"
}

get_toolchain

cp -rf bridge/main_bridge.sh  rootfs/utils/usr/sbin/main.sh

if [ $# -ne 0 ] && [ "$1" = "Sat" ]; then
    echo "SAT VERSION"
    cp -rf bridge/EVB_CBDM_AK3760E_SAT_V1.0.1.dts os/kernel/arch/arm/boot/dts/EVB_CBDM_AK3760E_V1.0.1.dts
else
    echo "MEIOU VERSION"
    cp -rf bridge/EVB_CBDM_AK3760E_V1.0.1.dts os/kernel/arch/arm/boot/dts/EVB_CBDM_AK3760E_V1.0.1.dts
fi


echo " ---------- build kenel---------- "
cd   ./os/kernel/
# make  mrproper
# make O=../bd mrproper
make O=../bd anycloud_ak37e_mini_defconfig CROSS_COMPILE=$CROSS_COMPILE
#make O=../bd menuconfig
make O=../bd -j8
make O=../bd dtbs modules uImage -j8 CROSS_COMPILE=$CROSS_COMPILE
cd ../../

echo " ---------- build image ---------- "

build_timestamp=$(date "+%m%d%H%M")

SAT_OS_CHECK=SAT_ANYKAOS_$build_timestamp
./make_image.sh "$SAT_OS_CHECK" "$1"


echo " ---------- copy image ---------- "
cp -rf os/bd/arch/arm/boot/dts/EVB_CBDM_AK3760E_V1.0.1.dtb ./image
cp -rf os/bd/arch/arm/boot/uImage ./image
cp -rf os/uboot/u-boot.bin ./image

cp -rf image/* tools/burntool
cp -rf image/* upgrade/platform


cd tools/envtool/
./build.sh 
cd -

echo " ---------- make anykaos ---------- "
./make_anykaos.sh $SAT_OS_CHECK

