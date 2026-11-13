#!/bin/sh
#在应用层只需修改app分区即可

#创建一个用于保存升级文件的目录
create_platform()
{
	if [ -d "platform" ]; then
                echo "remove platform dir"
                rm -rf "platform"
                mkdir "platform"
        else
                echo "mkdir platform"
                mkdir "platform"
        fi
}


#把 升级脚本 和 进度条显示程序 也放进去打包压缩
images_compress()
{
        cp scripts/update.sh                    platform/
        cp upgrade_progress/upgrade_progress    platform/

        rm -rf SAT_ANYKAOS*
        cd platform/
        tar -zcvf $SAT_OS_NAME *
        rm ../../SAT_ANYKAOS* -f
        mv $SAT_OS_NAME ../../
        cd ../
}

SAT_OS_NAME=$1

cd upgrade/

# create_platform

images_compress


