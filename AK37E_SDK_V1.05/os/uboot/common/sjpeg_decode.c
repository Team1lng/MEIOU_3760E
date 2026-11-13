
#include <malloc.h>
#include "tjpgd.h"
#define TJPGD_WORKBUFF_SIZE 4096
typedef struct
{
    uint8_t *raw_sjpg_data;               // Used when type==SJPEG_IO_SOURCE_C_ARRAY.
    uint32_t raw_sjpg_data_size;          // Num bytes pointed to by raw_sjpg_data.
    uint32_t raw_sjpg_data_next_read_pos; // Used for all types.
    uint8_t *img_cache_buff;
} io_source_t;

static size_t input_func(JDEC *jd, uint8_t *buff, size_t ndata)
{
    io_source_t *io = jd->device;

    if (!io)
        return 0;

    const uint32_t bytes_left = io->raw_sjpg_data_size - io->raw_sjpg_data_next_read_pos;
    const uint32_t to_read = ndata <= bytes_left ? (uint32_t)ndata : bytes_left;

    if (to_read == 0)
        return 0;

    if (buff)
    {
        memcpy(buff, io->raw_sjpg_data + io->raw_sjpg_data_next_read_pos, to_read);
    }

    io->raw_sjpg_data_next_read_pos += to_read;
    return to_read;
}

static int img_data_cb(JDEC *jd, void *data, JRECT *rect)
{
    io_source_t *io = jd->device;
    uint8_t *cache = io->img_cache_buff;
    uint8_t *buf = data;
    const int INPUT_PIXEL_SIZE = 3;
    const int row_width = rect->right - rect->left + 1; // Row width in pixels.
    const int row_size = row_width * INPUT_PIXEL_SIZE;  // Row size (bytes).
    int y = 0;
    for (y = rect->top; y <= rect->bottom; y++)
    {
        int row_offset = y * jd->width * INPUT_PIXEL_SIZE + rect->left * INPUT_PIXEL_SIZE;

        memcpy(cache + row_offset, buf, row_size);
        buf += row_size;
    }
}
int uboot_logo_sjpg_decode(unsigned char *src, int len, unsigned char *lcd_base)
{
    io_source_t io_source;
    io_source.raw_sjpg_data = src;
    io_source.raw_sjpg_data_size = len;
    io_source.raw_sjpg_data_next_read_pos = 0;
    io_source.img_cache_buff = lcd_base;
    uint8_t *wokb_temp = malloc(TJPGD_WORKBUFF_SIZE);

    JDEC jd;
    JRESULT rc = jd_prepare(&jd, input_func, wokb_temp, (size_t)TJPGD_WORKBUFF_SIZE, &io_source);
    free(wokb_temp);

    if (rc != JDR_OK)
    {
        printf("logo jpg prepare failed \n");
        return -1;
    }
    printf("jpg:w=%d,h=%d \n", jd.width, jd.height);
    rc = jd_decomp(&jd, img_data_cb, 0);
    if (rc != JDR_OK)
    {
        printf("logo jpg decode failed \n");
        return -1;
    }
    return 0;
}