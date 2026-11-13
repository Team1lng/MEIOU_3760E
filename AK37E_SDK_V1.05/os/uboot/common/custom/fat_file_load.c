/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2024-06-27 15:30:04
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2024-09-23 08:31:33
 * @FilePath: /uboot/common/custom/upgrade_file_load.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <fs.h>
#include <fat.h>
#include <common.h>
#define UPGRADE_BASE_ADDR "0x80008000"
#define DEFAULT_UPGRADE_DEVICE "spi"
#define TFTP_SERVER_CONFIG "tftpboot.conf"
#define UPGRADE_DEFAULT_NAME "SAT_ANYKA.IMG"
#define UPGRADE_NAME (getenv("bootfile") ? getenv("bootfile") : UPGRADE_DEFAULT_NAME)

int do_tftpb(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[]);

static void fat32_file_load_memory(char *dev, char *part, char *addr, int size)
{
    char hex_str[32] = {0};
    sprintf(hex_str, "0x%x", size);

    char *arg[7] = {0};
    arg[1] = dev;
    arg[2] = part;
    arg[3] = addr;
    arg[4] = UPGRADE_NAME;
    arg[5] = hex_str;
    if (do_load(NULL, 0, sizeof(arg) / sizeof(char *), arg, FS_TYPE_FAT))
    {
        printf("\033[33m [%s]:[%d],do_load %s fail\033[0m\n", __func__, __LINE__, arg[0]);
    }
}

static loff_t fat32_file_size(char *dev, char *part)
{
    if (fs_set_blk_dev(dev, part, FS_TYPE_FAT))
        return -1;
    int ret;
    ret = fat_read_file(UPGRADE_NAME, NULL, 0, 0);
    if (ret == -1)
    {
        printf("** Unable to read upgrade file %s **\n", UPGRADE_NAME);
        return -1;
    }
    printf("[%s]Get %s file size:%d\n", __func__, UPGRADE_NAME, ret);
    return ret;
}

loff_t fat32_upgrade_check(char **dev, char **part, char **file_name)
{
    loff_t size = -1;
    int dev_index = 0;
    char *dev_part[][2] = {{DEFAULT_UPGRADE_DEVICE, "0:0"}, {"mmc", "0"}};
    for (dev_index = 0; dev_index < sizeof(dev_part) / sizeof(dev_part[0]); dev_index++)
    {
        if ((size = fat32_file_size(dev_part[dev_index][0], dev_part[dev_index][1])) >= 0)
        {
            break;
        }
    }

    if (size <= 0)
        return -1;

    char *loadaddr = getenv("loadaddr");
    if (!loadaddr)
        return -1;

    *file_name = UPGRADE_NAME;
    *dev = dev_part[dev_index][0];
    *part = dev_part[dev_index][1];

    char cmd[128] = {0};
    sprintf(cmd, "mw.b %s 0x0 0x%llx", loadaddr, size + 1);
    run_command(cmd, 0);

    fat32_file_load_memory(dev_part[dev_index][0], dev_part[dev_index][1], loadaddr, size);

    return size;
}

static int get_conf_info(char *headr, const char *dest, char *info)
{
    char *enter = NULL;
    while (1)
    {
        headr = strstr(headr, dest);
        if (headr == NULL)
        {
            return 0;
        }

        headr += strlen(dest);
        if (headr)
        {
            enter = strchr(headr, '\n');
            if (enter)
                memcpy(info, headr, enter - headr);
            else
                memcpy(info, headr, strlen(headr));
            return 1;
        }
    }
    return 1;
}

static int tftpboot_service_addr_check(void)
{
    int ret;
    if ((ret = fs_set_blk_dev(DEFAULT_UPGRADE_DEVICE, "0:0", FS_TYPE_FAT)))
        return -1;

    printf("[%s][%d]%d\n", __func__, __LINE__, ret);
    char buf[128] = {0};
    ret = fat_read_file(TFTP_SERVER_CONFIG, buf, 0, sizeof(buf) - 1);
    if (ret < 0)
    {
        printf("** Unable to read config file %s **\n", TFTP_SERVER_CONFIG);
        return -1;
    }

    char serverip[16] = {0};
    char bootfile[32] = {0};
    int is_tftpfile_valid = 0;
    if (get_conf_info(buf, "serverip=", serverip))
    {
        is_tftpfile_valid = 1;
        printf("%s serverip:%s\n", TFTP_SERVER_CONFIG, serverip);
        setenv("serverip", serverip);
    }
    if (get_conf_info(buf, "bootfile=", bootfile))
    {
        is_tftpfile_valid = 1;
        setenv("bootfile", bootfile);
        printf("%s bootfile:%s\n", TFTP_SERVER_CONFIG, bootfile);
    }

    if (is_tftpfile_valid)
    {
        saveenv();
        return 0;
    }
    return -1;
}

loff_t tftpboot_upgrade_check(char **dev, char **part, char **file_name)
{
    if (tftpboot_service_addr_check() == -1)
    {
        // fs_unlink(TFTP_SERVER_CONFIG); /* 删除文件 */
        return 0;
    }
    *dev = DEFAULT_UPGRADE_DEVICE;
    *part = "0:0";
    *file_name = TFTP_SERVER_CONFIG;

    loff_t size = do_tftpb(NULL, 0, 1, 0);
    if (!size)
    {
        char *filesize = getenv("filesize");
        size = simple_strtoul(filesize, NULL, 16);
        printf("do_tftpb:%lld\n", size);
    }
    return size == 1 || size < 0 ? 0 : size;
}
