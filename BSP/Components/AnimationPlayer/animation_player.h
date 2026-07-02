/**
  ******************************************************************************
  * @file    animation_player.h
  * @brief   Animation player – reads PACK/ANIM binary from W25Q128,
  *          decodes RLE frames, displays on SSD1331 (96x64 RGB565).
  ******************************************************************************
  *
  * Binary layout produced by pack_animations.py:
  *
  *  ┌─ PACK header ──────────────────────────────┐
  *  │ "PACK"  (4 B)                               │
  *  │ anim_count  (uint16 LE)                     │
  *  ├─ Table (40 B × anim_count) ────────────────┤
  *  │ name[32]  offset(u32)  size(u32)            │
  *  │ ...                                         │
  *  ├─ Animation block 0 ────────────────────────┤
  *  │ "ANIM"  (4 B)                               │
  *  │ frame_count (uint16 LE)                     │
  *  │ frame_table_offset (uint32 LE) = 18         │
  *  │ frame_data_size  (uint32 LE)                │
  *  │ sound_data_size  (uint32 LE)                │
  *  │ Frame table  (8 B × frame_count)            │
  *  │   offset_in_frame_data (u32) + size (u32)   │
  *  │ Frame data (concatenated RLE blobs)         │
  *  │ Sound data (raw IMA-ADPCM)                  │
  *  ├─ Animation block 1 ────────────────────────┤
  *  │ ...                                         │
  *  └────────────────────────────────────────────┘
  *
  * RLE frame format (each .rle file):
  *   Repeated entries:  count(u8) + color_hi(u8) + color_lo(u8)
  *   Total decoded pixels = 96 × 64 = 6144
  *
  ******************************************************************************
  */

#ifndef __ANIMATION_PLAYER_H__
#define __ANIMATION_PLAYER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* --- Configuration --------------------------------------------------------*/
#define ANIM_MAX_ANIMATIONS   16   /* max entries in PACK table kept in RAM  */
#define ANIM_MAX_FRAMES       256  /* max frames per single animation        */
#define ANIM_DEFAULT_FPS      15   /* frames per second                      */

/* --- Public types ---------------------------------------------------------*/

/** Entry in the PACK table (kept in RAM after AnimPlayer_Init) */
typedef struct {
    char     name[32];
    uint32_t offset;   /* absolute byte offset inside W25Q128 */
    uint32_t size;     /* total size of the ANIM block        */
} AnimPackEntry_t;

/** Parsed ANIM block header */
typedef struct {
    uint16_t frame_count;
    uint32_t frame_table_offset;  /* always 18 from start of ANIM block */
    uint32_t frame_data_size;
    uint32_t sound_data_size;
} AnimHeader_t;

/** Frame table entry */
typedef struct {
    uint32_t offset;  /* relative to start of frame data blob */
    uint32_t size;
} AnimFrameEntry_t;

/** Player state */
typedef struct {
    /* PACK-level */
    uint16_t        anim_count;
    AnimPackEntry_t entries[ANIM_MAX_ANIMATIONS];
    uint32_t        pack_base_addr;  /* W25Q128 address where PACK starts */

    /* Currently loaded animation */
    int16_t         cur_anim_idx;    /* -1 = none */
    AnimHeader_t    cur_header;
    uint32_t        cur_anim_addr;   /* absolute W25Q128 addr of ANIM block */
    uint32_t        cur_frame_data_addr; /* absolute addr of first frame byte */
    uint32_t        cur_sound_addr;  /* absolute addr of PCM sound data     */
    AnimFrameEntry_t frame_table[ANIM_MAX_FRAMES];

    /* Playback */
    uint16_t        cur_frame;
    uint8_t         fps;
    uint8_t         loop;           /* 1 = loop, 0 = play once */
    volatile uint8_t playing;
} AnimPlayer_t;

/* --- Public API -----------------------------------------------------------*/

/**
  * @brief  Initialise player: read PACK header + table from flash
  * @param  player: player instance
  * @param  flash_addr: base address in W25Q128 where animations.bin was written
  * @retval 0 on success, negative on error
  */
int AnimPlayer_Init(AnimPlayer_t *player, uint32_t flash_addr);

/**
  * @brief  Select an animation by index and load its frame table
  * @param  player: player instance
  * @param  index: animation index (0 .. anim_count-1)
  * @retval 0 on success, -1 on error
  */
int AnimPlayer_Select(AnimPlayer_t *player, uint16_t index);

/**
  * @brief  Select an animation by name
  * @param  player: player instance
  * @param  name:  null-terminated animation name
  * @retval 0 on success, -1 if not found
  */
int AnimPlayer_SelectByName(AnimPlayer_t *player, const char *name);

/**
  * @brief  Decode one RLE frame from flash and display it on SSD1331
  * @param  player: player instance
  * @param  frame_idx: frame index within current animation
  * @retval 0 on success
  */
int AnimPlayer_ShowFrame(AnimPlayer_t *player, uint16_t frame_idx);

/**
  * @brief  Play current animation (blocking, should run in a ThreadX thread).
  *         Displays frames at configured FPS, loops if player->loop is set.
  *         Returns when animation finishes (if not looping) or player->playing
  *         is cleared by another thread.
  * @param  player: player instance
  */
void AnimPlayer_Play(AnimPlayer_t *player);

/**
  * @brief  Stop playback (can be called from another thread)
  * @param  player: player instance
  */
void AnimPlayer_Stop(AnimPlayer_t *player);

/**
  * @brief  Get the number of animations in the pack
  */
uint16_t AnimPlayer_GetCount(const AnimPlayer_t *player);

/**
  * @brief  Get the name of an animation by index
  */
const char *AnimPlayer_GetName(const AnimPlayer_t *player, uint16_t index);

#ifdef __cplusplus
}
#endif

#endif /* __ANIMATION_PLAYER_H__ */
