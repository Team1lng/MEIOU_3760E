make;
###
 # @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @Date: 2024-10-25 16:43:25
 # @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @LastEditTime: 2024-10-25 16:48:29
 # @FilePath: /ws73_sdk_linux108_20240711/build.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 
cp -rf output/bin/sle_soc.ko ../../../../rootfs/ko/external/ws73v100/
cp -rf output/bin/wifi_soc.ko ../../../../rootfs/ko/external/ws73v100/
cp output/bin/ws73_cfg.ini ../../../../rootfs/resource/usr/share/firmware/
cp firmware/us/* ../../../../rootfs/resource/usr/share/firmware/
