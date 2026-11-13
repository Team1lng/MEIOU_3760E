#!/bin/sh
###
 # @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @Date: 2024-01-11 08:18:21
 # @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @LastEditTime: 2024-01-12 15:38:35
 # @FilePath: /AK37E_SDK_V1.05/tools/envtool/build.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 
# 一键制作分区表
rm env.img
tr '\000' '\377' < /dev/zero | dd of=./env.img bs=1024 count=4
#hlf:env_ak3761e_nor -> env_ak3760e_nor
./fw_setenv -s env_ak3761e_nor.cfg
./fw_printenv
cp -rf env.img  ../burntool/env_ak3761e_nor.img
cp -rf env.img  ../../image/env_ak3761e_nor.img