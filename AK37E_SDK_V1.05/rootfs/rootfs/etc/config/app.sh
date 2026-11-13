#!/bin/sh

# 运行flash里的app
run_flash_app()
{
    echo "run flash app"

    ANYKA37E_BIN_PATH="/app/app/ANYKA37E.BIN"

    while true; do
        # 检查文件是否存在
        if [ -e "$ANYKA37E_BIN_PATH" ]; then
            # 检查 ANYKA37E.BIN 是否在运行
            if pgrep ANYKA37E.BIN > /dev/null; then
                sleep 1
            else
                echo "ANYKA37E.BIN is not running, restarting..."
                "$ANYKA37E_BIN_PATH" &
                sleep 5  # 等待 5 秒再次检查
            fi
        else
            echo "ANYKA37E.BIN path not found: $ANYKA37E_BIN_PATH"
            exit 1
        fi
    done
}

# 挂载sd卡到/mnt/tf/目录下
mount_sd_card()
{
#!/bin/bash

# 检查是否已经插入了 SD 卡
if [ -e /dev/mmcblk0p1 ]; then
    DEV="/dev/mmcblk0p1" # 定义 $DEV 变量
    echo "SD card inserted, starting to mount $DEV"
    # 创建挂载点
    mount_point="/mnt/tf"
    if [ ! -d "$mount_point" ]; then
        mkdir "$mount_point"
    fi
    # 挂载 SD 卡
    if mount "$DEV" "$mount_point" 2> /dev/null; then
        echo "SD card mounted on $mount_point."
    else
        echo "Failed to mount SD card on $mount_point."
    fi

    elif [ -e /dev/mmcblk0 ]; then
    DEV="/dev/mmcblk0" # 定义 $DEV 变量
    echo "SD card inserted, starting to mount $DEV"
    # 创建挂载点
    mount_point="/mnt/tf"
    if [ ! -d "$mount_point" ]; then
        mkdir "$mount_point"
    fi

    # 挂载 SD 卡
    if mount "$DEV" "$mount_point" 2> /dev/null; then
        echo "SD card mounted on $mount_point."
    else
        echo "Failed to mount SD card on $mount_point."
    fi

else
    echo "SD card not inserted"
    run_flash_app
fi
}

# 检测升级包是否存在，存在则启动升级脚本
upgrade()
{
    file_name=$(ls -p /mnt/tf/ | grep '^SAT_ANYKAOS' | sed 's|/$||')
    
    if [ -z "$file_name" ]; then
        echo "Upgrade package $file_name does not exist "
        run_flash_app
    else
        echo "Upgrade package exists, check upgrade"
        if [ -e /app/data/$file_name ]; then
            echo "Upgraded,run app"
            run_flash_app
        else
            echo "Not upgraded,starting upgrade $file_name..."
            rm -f /app/data/SAT_ANYKAOS*
            rm -f /app/data/user_data.cfg
            cp -f /mnt/tf/$file_name /tmp
            tar -zxvf /tmp/$file_name -C /tmp
            /tmp/update.sh  "$file_name"

            exit
        fi
    fi
}



mount_sd_card

upgrade

