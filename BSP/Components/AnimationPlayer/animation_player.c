/**
  ******************************************************************************
  * @file    animation_player.c
  * @brief   Animation player – reads PACK/ANIM from W25Q128, decodes RLE,
  *          displays on SSD1331.
  ******************************************************************************
  */

#include "animation_player.h"
#include "audio_player.h"
#include "w25q128.h"
#include "ssd1331.h"
#include "tx_api.h"
#include <string.h>

/* --- Private helpers -------------------------------------------------------*/

/** Read a little-endian uint16 from buffer */
static inline uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/** Read a little-endian uint32 from buffer */
static inline uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* --- Public API ------------------------------------------------------------*/

int AnimPlayer_Init(AnimPlayer_t *player, uint32_t flash_addr)
{
    memset(player, 0, sizeof(*player));
    player->cur_anim_idx = -1;
    player->fps          = ANIM_DEFAULT_FPS;
    player->loop         = 1;
    player->pack_base_addr = flash_addr;

    /* Read PACK header: "PACK" (4) + count (2) = 6 bytes */
    uint8_t hdr[6];
    W25Q128_Read(flash_addr, hdr, 6);

    if (hdr[0] != 'P' || hdr[1] != 'A' || hdr[2] != 'C' || hdr[3] != 'K')
        return -1;  /* bad magic */

    player->anim_count = rd_u16(&hdr[4]);

    if (player->anim_count > ANIM_MAX_ANIMATIONS)
        player->anim_count = ANIM_MAX_ANIMATIONS;

    /* Read animation table: 40 bytes per entry */
    uint32_t table_addr = flash_addr + 6;

    for (uint16_t i = 0; i < player->anim_count; i++)
    {
        uint8_t entry[40];
        W25Q128_Read(table_addr + (uint32_t)i * 40, entry, 40);

        memcpy(player->entries[i].name, entry, 32);
        player->entries[i].name[31] = '\0';  /* ensure null-term */
        player->entries[i].offset = rd_u32(&entry[32]);
        player->entries[i].size   = rd_u32(&entry[36]);
    }

    return 0;
}

int AnimPlayer_Select(AnimPlayer_t *player, uint16_t index)
{
    if (index >= player->anim_count)
        return -1;

    uint32_t anim_addr = player->pack_base_addr + player->entries[index].offset;
    player->cur_anim_addr = anim_addr;
    player->cur_anim_idx  = (int16_t)index;

    /* Read ANIM header: "ANIM"(4) + frame_count(2) + table_off(4) +
       frame_data_size(4) + sound_data_size(4) = 18 bytes */
    uint8_t hdr[18];
    W25Q128_Read(anim_addr, hdr, 18);

    if (hdr[0] != 'A' || hdr[1] != 'N' || hdr[2] != 'I' || hdr[3] != 'M')
        return -1;

    player->cur_header.frame_count        = rd_u16(&hdr[4]);
    player->cur_header.frame_table_offset = rd_u32(&hdr[6]);
    player->cur_header.frame_data_size    = rd_u32(&hdr[10]);
    player->cur_header.sound_data_size    = rd_u32(&hdr[14]);

    uint16_t fc = player->cur_header.frame_count;
    if (fc == 0)
        return -1;
    if (fc > ANIM_MAX_FRAMES)
        fc = ANIM_MAX_FRAMES;

    /* Read frame table: 8 bytes per frame */
    uint32_t ftable_addr = anim_addr + player->cur_header.frame_table_offset;

    for (uint16_t i = 0; i < fc; i++)
    {
        uint8_t e[8];
        W25Q128_Read(ftable_addr + (uint32_t)i * 8, e, 8);
        player->frame_table[i].offset = rd_u32(&e[0]);
        player->frame_table[i].size   = rd_u32(&e[4]);
    }

    /* Absolute address of frame data blob */
    player->cur_frame_data_addr = ftable_addr + (uint32_t)fc * 8;

    /* Sound data sits right after frame data */
    player->cur_sound_addr = player->cur_frame_data_addr +
                             player->cur_header.frame_data_size;

    player->cur_frame = 0;
    return 0;
}

int AnimPlayer_SelectByName(AnimPlayer_t *player, const char *name)
{
    for (uint16_t i = 0; i < player->anim_count; i++)
    {
        if (strncmp(player->entries[i].name, name, 31) == 0)
            return AnimPlayer_Select(player, i);
    }
    return -1;
}

/**
  * @brief  Decode one RLE frame from flash and send to SSD1331
  *
  * RLE format per frame:
  *   Repeated triplets: count(u8) + color_hi(u8) + color_lo(u8)
  *   Decoded pixel count must equal SSD1331_WIDTH * SSD1331_HEIGHT = 6144
  *
  * Strategy:
  *   1) Read the entire RLE blob from W25Q128 into a stack buffer
  *      (max ~2 KB for a 96x64 RLE frame – worst case 6144 × 3 / 1 but
  *       RLE compresses well; we cap at ANIM_RLE_BUF_SIZE).
  *   2) Set SSD1331 window, then decode RLE into a scanline buffer and
  *      push each line to the display.
  *
  * SPI1 bus safety: W25Q128 CS is fully released before SSD1331 CS is
  * asserted, so no bus conflict even though they share MOSI/MISO/SCK.
  */
#define ANIM_RLE_BUF_SIZE  4096  /* enough for most 96x64 RLE frames */

int AnimPlayer_ShowFrame(AnimPlayer_t *player, uint16_t frame_idx)
{
    if (player->cur_anim_idx < 0)
        return -1;
    if (frame_idx >= player->cur_header.frame_count)
        return -1;

    uint32_t rle_addr = player->cur_frame_data_addr +
                        player->frame_table[frame_idx].offset;
    uint32_t rle_size = player->frame_table[frame_idx].size;

    if (rle_size > ANIM_RLE_BUF_SIZE)
        return -2;  /* frame too large */

    /* --- Phase 1: read entire RLE blob from flash (W25Q CS cycle) --- */
    static uint8_t rle_buf[ANIM_RLE_BUF_SIZE]; /* static: avoid 4KB stack overflow */
    W25Q128_Read(rle_addr, rle_buf, rle_size);

    /* --- Phase 2: decode RLE → push scanlines to SSD1331 --- */
    SSD1331_SetWindow(0, 0, SSD1331_WIDTH - 1, SSD1331_HEIGHT - 1);
    SSD1331_WriteCommand(SSD1331_CMD_WRITERAM);

    uint8_t line_buf[SSD1331_WIDTH * 2]; /* one scanline = 192 bytes */
    uint16_t px_in_line = 0;
    uint32_t rle_pos = 0;

    /* Start OLED data stream */
    SSD1331_StartStream();

    while (rle_pos + 2 < rle_size)
    {
        uint8_t count = rle_buf[rle_pos];
        uint8_t hi    = rle_buf[rle_pos + 1];
        uint8_t lo    = rle_buf[rle_pos + 2];
        rle_pos += 3;

        for (uint8_t c = 0; c < count; c++)
        {
            line_buf[px_in_line * 2]     = hi;
            line_buf[px_in_line * 2 + 1] = lo;
            px_in_line++;

            if (px_in_line >= SSD1331_WIDTH)
            {
                HAL_SPI_Transmit(&hspi1, line_buf, SSD1331_WIDTH * 2, HAL_MAX_DELAY);
                px_in_line = 0;
            }
        }
    }

    /* Flush any remaining partial line */
    if (px_in_line > 0)
        HAL_SPI_Transmit(&hspi1, line_buf, px_in_line * 2, HAL_MAX_DELAY);

    SSD1331_EndStream();

    return 0;
}

void AnimPlayer_Play(AnimPlayer_t *player)
{
    if (player->cur_anim_idx < 0)
        return;

    player->playing   = 1;
    player->cur_frame = 0;

    /* Start audio if this animation carries sound data */
    if (player->cur_header.sound_data_size > 0)
        AudioPlayer_Start(player->cur_sound_addr,
                          player->cur_header.sound_data_size);

    ULONG ticks_per_frame = TX_TIMER_TICKS_PER_SECOND / player->fps;
    if (ticks_per_frame == 0)
        ticks_per_frame = 1;

    uint16_t total_frames = player->cur_header.frame_count;

    while (player->playing)
    {
        ULONG t0 = tx_time_get();

        AnimPlayer_ShowFrame(player, player->cur_frame);

        /* Refill audio DMA buffer after each frame display */
        AudioPlayer_Feed();

        player->cur_frame++;
        if (player->cur_frame >= total_frames)
        {
            if (player->loop)
                player->cur_frame = 0;
            else
            {
                player->playing = 0;
                break;
            }
        }

        /* Sleep for remainder of frame period */
        ULONG elapsed = tx_time_get() - t0;
        if (elapsed < ticks_per_frame)
            tx_thread_sleep(ticks_per_frame - elapsed);
    }

    AudioPlayer_Stop();
}

void AnimPlayer_Stop(AnimPlayer_t *player)
{
    player->playing = 0;
    AudioPlayer_Stop();
}

uint16_t AnimPlayer_GetCount(const AnimPlayer_t *player)
{
    return player->anim_count;
}

const char *AnimPlayer_GetName(const AnimPlayer_t *player, uint16_t index)
{
    if (index >= player->anim_count)
        return "";
    return player->entries[index].name;
}
