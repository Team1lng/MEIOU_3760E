/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2024-06-15 14:18:44
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2024-07-04 08:03:23
 * @FilePath: /uboot/drivers/demo/nor_fat_uclass.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include <common.h>
#include "part.h"
#include <div64.h>
#include "spi_flash.h"
#include <jffs2/load_kernel.h>

static struct spi_flash *flash;
static block_dev_desc_t spi_block_dev;
static struct part_info *part = NULL;

static ulong spi_flash_bread(int dev_num, lbaint_t start, lbaint_t blkcnt, void *dst)
{
    spi_flash_read(flash, part->offset + spi_block_dev.blksz * start, blkcnt * spi_block_dev.blksz, dst);
    return blkcnt;
}

static ulong spi_write_bwrite(int dev_num, lbaint_t start, lbaint_t blkcnt, void *src)
{
    spi_flash_erase(flash, part->offset + spi_block_dev.blksz * start, blkcnt * spi_block_dev.blksz);
    spi_flash_write(flash, part->offset + spi_block_dev.blksz * start, blkcnt * spi_block_dev.blksz, src);
    return blkcnt;
}

unsigned long spi_erase_berase(int dev_num, lbaint_t start, lbaint_t blkcnt)
{
    spi_flash_erase(flash, part->offset + spi_block_dev.blksz * start, blkcnt * spi_block_dev.blksz);
    return blkcnt;
}

static int nor_fat_blk_probe(void)
{
    int ret = 0;
    flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS,
                            CONFIG_SF_DEFAULT_CS,
                            CONFIG_SF_DEFAULT_SPEED,
                            CONFIG_SF_DEFAULT_MODE);

    if (!flash)
    {
        puts("Nor probe failed.\n");
        return -1;
    }

    return ret;
}

block_dev_desc_t *spi_get_dev(int dev)
{
    uint8_t pnum;
    struct mtd_device *mtd_dev;
    if (nor_fat_blk_probe())
    {
        return NULL;
    }

    mtdparts_init();
    if (find_dev_and_part("FAT", &mtd_dev, &pnum, &part) != 0)
    {
        puts("Find part FAT failed.\n");
        return NULL;
    }
    printf("Part:%s,Start:0x%llx,Size:%lluKB!!!\n", part->name, part->offset, part->size / 1024);

    spi_block_dev.if_type = IF_TYPE_SPI;
    spi_block_dev.dev = dev;
    spi_block_dev.blksz = 4096;
    spi_block_dev.lba = (180 * 1024) / spi_block_dev.blksz;
    spi_block_dev.log2blksz = LOG2(spi_block_dev.blksz);
    spi_block_dev.block_read = spi_flash_bread;
    spi_block_dev.block_write = spi_write_bwrite;
    spi_block_dev.block_erase = spi_erase_berase;
#if !defined(CONFIG_SPL_BUILD) || defined(CONFIG_SPL_LIBDISK_SUPPORT)
    init_part(&spi_block_dev);
#endif
    return &spi_block_dev;
}