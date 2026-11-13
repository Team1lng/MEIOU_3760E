#!/bin/bash
###
 # @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @Date: 2024-06-04 19:48:29
 # @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 # @LastEditTime: 2024-08-10 15:58:37
 # @FilePath: /AK37E_SDK_V1.05/auto_build.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 
clear;

#选择编译对象
SelectCompiledObject()
{
    echo "What is your Compile Object?"
    select Object in "Sat" "Meiou"; 
        do echo "You have selected $Object"
        break
    done
}

# 选择wifi型号
SelectWifiModel()
{
    CONFIG_MK="config.mk"
    echo "Please select wifi model?"
    select Wifi in "NO_WIFI" "atbm6031" "atbm6032" "rtl8188ftv" "rtl8189ftv" "ssv6x5x" "hi3881" "ws73v100" "rtl8731ftv"; 
        do echo "You have selected $Wifi"
        break
    done

    if [ "$Wifi" = "NO_WIFI" ]; then
        sed -i 's/^WIFI_TYPE.*/WIFI_TYPE    =NO_WIFI/' $CONFIG_MK
    else
        sed -i 's/^WIFI_TYPE.*/WIFI_TYPE    ='"${Wifi}"'/' $CONFIG_MK
    fi
}

# 编译模块
SelectCompiledModules()
{
    echo -n "Compiled Uboot $CompiledUboot? [y/n]"
    read -n 2 CompiledUboot
    if [ "$CompiledUboot" = "y" ]; then
        cd os
        # ./build_uboot.sh -c
        ./build_uboot.sh 
        if test $? -ne 0 ; then
            echo "Uboot build failed!"
            cd -
            exit
        fi
        cd -
    fi

    echo -n "Compiled Kernel $CompiledKernel? [y/n]"
    read -n 2 CompiledKernel
    if [ "$CompiledKernel" = "y" ]; then
        ./build_kernel.sh $Object
        if test $? -ne 0 ; then
            echo "Kernel build failed!"
            exit
        fi
    fi
}

SelectCompiledObject
SelectWifiModel
SelectCompiledModules