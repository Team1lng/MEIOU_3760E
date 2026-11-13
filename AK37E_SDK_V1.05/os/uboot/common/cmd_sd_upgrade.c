/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2024-01-08 20:59:38
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2024-07-15 08:34:34
 * @FilePath: /AK37E_SDK_V1.05/os/uboot/common/cmd_sd_upgrade.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include <common.h>
#include <fs.h>
#include <fat.h>
#define UPGRADE_BASE_ADDR "0x80008000"
#define UPGRADE_NAME "SAT_ANYKA.IMG"
#define UPGRADE_INFO_PATH "# File Parttion: "
#define UPGRADE_VERSION_PATH "#<upgrade_bin_version="
#define UPGRADE_IMAGE_BASE_PATH "# <- this is end of image parttion\n"

typedef struct
{
    char name[32];
    unsigned long fileoffset;
    unsigned long partitionoffset;
    unsigned long partitionsize;
    unsigned long filesize;
} upgrade_info;

struct
{
    upgrade_info info[12];
    char *buffer;
    int total;
    unsigned long upgrade_image_size;
    unsigned long upgrade_bin_size;
    unsigned long upgrade_offset_base;
    char upgrade_version[32];
} upgrade = {.total = 0};

static const char *strrstr(const char *start_str, const char *end_str, const char *dest_str)
{
    const char *p1 = start_str;
    const char *p2 = NULL;
    while (1)
    {
        p1 = strstr(p1, dest_str);
        if (p1 == NULL)
        {
            break;
        }
        if (p1 < end_str)
        {
            p2 = p1 = p1 + strlen(dest_str);
        }
        else
        {
            break;
        }
    }
    return p2;
}

static int upgrade_draw_rect(int x, int y, int w, int h, unsigned int color)
{
    int i, j;
    static void *fb_base = NULL;
    if (fb_base == NULL)
    {
        fb_base = (void *)getenv_ulong("lcdloadaddr", 16, 0x83d02000);
    }
    char *src = fb_base + (y * 1024 + x) * 3;
    for (i = 0; i < h; i++)
    {
        for (j = 0; j < w; j++)
        {
            src[i * 1024 * 3 + j * 3] = color & 0xFF;
            src[i * 1024 * 3 + j * 3 + 1] = (color >> 8) & 0xFF;
            src[i * 1024 * 3 + j * 3 + 2] = (color >> 16) & 0xFF;
        }
    }
}

static int upgrade_version_check(const char *sour)
{
    char *p = NULL;
    p = getenv("uprade_image_version");

    char version[128] = {0};
    memcpy(version, sour, sizeof(version) - 1);
    version[127] = '\0';

    char *start_str = strstr(version, UPGRADE_VERSION_PATH);
    if (start_str == NULL || !(start_str += strlen(UPGRADE_VERSION_PATH)))
    {
        return 0;
    }

    char *end_str = strstr(start_str, ">");
    if (end_str == NULL)
    {
        return 0;
    }

    int len = end_str - start_str;
    if (len > sizeof(upgrade.upgrade_version))
    {
        return 0;
    }

    memcpy(upgrade.upgrade_version, start_str, len);
    if (p != NULL)
    {
        if (strncmp(p, upgrade.upgrade_version, strlen(upgrade.upgrade_version)) == 0)
        {
            printf("[%s]:[%d] Version consistency\n", __func__, __LINE__);
            return 0;
        }
        printf("[%s]:[%d]upgrade_version:%s,env version:%s\n", __func__, __LINE__, upgrade.upgrade_version, p);
    }
    return 1;
}

//解析升级文件
static int upgrade_file_info_pares(const char *sour, upgrade_info *info_group, int *total, int info_max)
{
    if (info_max == 0)
    {
        return 0;
    }
#define SPLIT_SYMBOL ", "
    memset(info_group, 0, sizeof(upgrade_info));
    char *start_str = strstr(sour, UPGRADE_INFO_PATH);

    if (start_str == NULL || !(start_str += strlen(UPGRADE_INFO_PATH)))
    {
        return 0;
    }

    char *end_str = strstr(start_str, SPLIT_SYMBOL);
    if (end_str == NULL)
    {
        return 0;
    }
    memset(info_group->name, 0, sizeof(info_group->name));
    memcpy(info_group->name, start_str, end_str - start_str);

    start_str = end_str + strlen(SPLIT_SYMBOL);
    if (start_str == NULL)
        return 0;

    info_group->fileoffset = simple_strtoul(start_str, &start_str, 10);

    start_str += strlen(SPLIT_SYMBOL);
    if (start_str == NULL)
        return 0;

    info_group->filesize = simple_strtoul(start_str, &start_str, 10);
    (*total)++;

    return upgrade_file_info_pares(start_str, info_group + 1, total, --info_max);
}

//升级镜像文件在存储设备中的基础偏移量
static int upgrade_image_base_offset(const char *sour)
{
    char *base = strstr(sour, UPGRADE_IMAGE_BASE_PATH);
    if (base == NULL || !(base += strlen(UPGRADE_IMAGE_BASE_PATH)))
    {
        printf("\033[33m not found upgrade_base!!!\033[0m\n\n");
        return -1;
    }

    int base_offset = base - sour;

    upgrade.upgrade_offset_base = (unsigned long)upgrade.buffer + base_offset;

    upgrade.upgrade_bin_size = upgrade.upgrade_image_size - base_offset;
    return base_offset;
}

static int upgrade_file_size(void)
{
    if (fs_set_blk_dev("mmc", "0", FS_TYPE_FAT))
    {
        return -1;
    }

    unsigned int size;
    size = fat_read_file(UPGRADE_NAME, NULL, 0, 0);
    if (size == -1)
    {
        printf("** Unable to read upgrade file %s **\n", UPGRADE_NAME);
        return -1;
    }
    return size;
}

static int upgrade_env_img_parse(const char *sour)
{
    int i;
    const char *p = NULL;
    for (i = 0; i < upgrade.total; i++)
    {
        if (strncmp(upgrade.info[i].name, "ENV", 3) == 0)
        {
            int count = 100;
            char *env_addr = (char *)(upgrade.upgrade_offset_base + upgrade.info[i].fileoffset);
            while (count--)
            {
                char *mtdparts = strstr(env_addr, "mtdparts=");
                if (mtdparts)
                {
                    break;
                }
                else
                    env_addr += strlen(env_addr) + 1;
            }
            break;
        }
    }

    if (p == NULL)
    {
        p = getenv("mtdparts");
        if (p == NULL)
        {
            return 0;
        }
    }

    for (i = 0; i < upgrade.total; i++)
    {
        if (upgrade.info[i].filesize != 0)
        {
            char *name = strstr(p, upgrade.info[i].name);
            if (name != NULL)
            {
                const char *parttion_size = strrstr(p, name, ",");

                if (parttion_size == NULL)
                {
                    parttion_size = strrstr(p, name, ":");
                    if (parttion_size == NULL)
                    {
                        break;
                    }
                }

                const char *parttion_offset = strrstr(p, name, "@");
                if (!parttion_offset)
                {
                    break;
                }
                upgrade.info[i].partitionsize = simple_strtoul(parttion_size, NULL, 10) * 1024;
                upgrade.info[i].partitionoffset = simple_strtoul(parttion_offset, NULL, 16);
            }
        }
    }
    return 1;
}

static int upgrade_img_head_parse(const char *sour)
{
    char buffer[1024] = {0};
    memcpy(buffer, sour, sizeof(buffer) - 1);

    upgrade_file_info_pares(buffer, &upgrade.info[0], &upgrade.total, sizeof(upgrade.info) / sizeof(upgrade_info));

    upgrade_image_base_offset(buffer);

    upgrade_env_img_parse(buffer);
}

static int update_flash_partition(void)
{
    int i;
    unsigned long bin_size = 0;
    run_command("sf probe", 0);
    upgrade_draw_rect(0, 0, 1024, 600, 0x0);
    upgrade_draw_rect(312, 290, 400, 20, 0xFFFFFF);
    for (i = 0; i < upgrade.total; i++)
    {
        if (upgrade.info[i].filesize <= upgrade.info[i].partitionsize)
        {
            printf("+++start bin+++\n");
            printf("partition_name:%s\n", upgrade.info[i].name);
            printf("file_size:%lu\n", upgrade.info[i].filesize);
            printf("file_offset:%lu\n", upgrade.info[i].fileoffset);
            printf("partition_size:%luK\n", upgrade.info[i].partitionsize / 1024);
            printf("partition_offset:0x%lx\n", upgrade.info[i].partitionoffset);

            char cmd[128] = {0};
            sprintf(cmd, "sf erase 0x%lx 0x%lx", upgrade.info[i].partitionoffset, upgrade.info[i].partitionsize);
            run_command(cmd, 0);
            upgrade_draw_rect(312, 290, bin_size * 400 / upgrade.upgrade_bin_size, 20, 0xff9326);
            bin_size += (upgrade.info[i].filesize / 2);

            memset(cmd, 0, sizeof(cmd));
            sprintf(cmd, "sf write 0x%lx 0x%lx 0x%lx", upgrade.upgrade_offset_base + upgrade.info[i].fileoffset, upgrade.info[i].partitionoffset, upgrade.info[i].filesize);
            run_command(cmd, 0);
            upgrade_draw_rect(312, 290, bin_size * 400 / upgrade.upgrade_bin_size, 20, 0xff9326);
            bin_size += (upgrade.info[i].filesize - (upgrade.info[i].filesize / 2));
        }
        else
        {
            upgrade_draw_rect(312, 290, bin_size * 400 / upgrade.upgrade_bin_size, 20, 0xff9326);
            bin_size += upgrade.info[i].filesize;
        }
    }
    upgrade_draw_rect(312, 290, 400, 20, 0xff9326);
    env_relocate_spec();
    setenv("uprade_image_version", upgrade.upgrade_version);
    saveenv();
    mdelay(3000);
    return 0;
}

static void upgrade_load_memory(char *addr, int size)
{
    char hex_str[32] = {0};
    sprintf(hex_str, "0x%x", size);

    char *arg[7] = {0};
    arg[1] = "mmc";
    arg[2] = "0";
    arg[3] = addr;
    arg[4] = UPGRADE_NAME;
    arg[5] = hex_str;
    if (do_load(NULL, 0, sizeof(arg) / sizeof(char *), arg, FS_TYPE_FAT))
    {
        printf("\033[33m [%s]:[%d],do_load %s fail\033[0m\n", __func__, __LINE__, arg[0]);
    }
}

static int memory_initialization(void)
{
    char cmd[128] = {0};
    upgrade.upgrade_image_size = upgrade_file_size();
    sprintf(cmd, "mw.b %s 0x0 0x%lx", UPGRADE_BASE_ADDR, upgrade.upgrade_image_size + 1);
    run_command(cmd, 0);

    upgrade.buffer = getenv("loadaddr");
    if (upgrade.buffer != NULL)
    {
        upgrade_load_memory(upgrade.buffer, upgrade.upgrade_image_size);
        upgrade.buffer = (char *)simple_strtoul(upgrade.buffer, NULL, 16);
    }
    else
    {
        upgrade_load_memory(UPGRADE_BASE_ADDR, upgrade.upgrade_image_size);
        upgrade.buffer = (char *)simple_strtoul(UPGRADE_BASE_ADDR, NULL, 16);
    }
    return 1;
}

static int do_sd_upgrade(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
    memory_initialization();

    if (upgrade_version_check(upgrade.buffer))
    {
        upgrade_img_head_parse(upgrade.buffer);

        update_flash_partition();

        run_command("reset", 0);
    }
    return 0;
}

U_BOOT_CMD(
    sd_upgrade, 1, 0, do_sd_upgrade,
    NULL,
    NULL);
