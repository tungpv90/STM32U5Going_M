/**
  ******************************************************************************
  * @file    audio_player.c
  * @brief   Audio driver for MAX98357A via SAI1 Block A + DMA.
  *          Reads IMA-ADPCM from W25Q128, decodes to 16-bit PCM on the fly,
  *          and streams through SAI1 to the MAX98357A I2S amplifier.
  ******************************************************************************
  *
  *  IMA ADPCM format (as produced by convert_audio.py / ffmpeg):
  *    Organised in 1024-byte blocks.  Each block:
  *      [0..1]  int16_LE  predicted sample (initial state)
  *      [2]     uint8     step index
  *      [3]     0x00      reserved
  *      [4..1023]  packed nibble pairs (low nibble first)
  *    Each block decodes to: 1 (header sample) + 2 × 1020 = 2041 samples.
  *
  ******************************************************************************
  */

#include "audio_player.h"
#include "w25q128.h"
#include "main.h"
#include "sai.h"
#include <string.h>
#include <stdio.h>

/* --- SAI handle (from CubeMX generated headers) --------------------------*/
/* hsai_BlockA1 → sai.h */

/* --- IMA ADPCM tables -----------------------------------------------------*/

static const int16_t ima_step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,
       16,    17,    19,    21,    23,    25,    28,    31,
       34,    37,    41,    45,    50,    55,    60,    66,
       73,    80,    88,    97,   107,   118,   130,   143,
      157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,
      724,   796,   876,   963,  1060,  1166,  1282,  1411,
     1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
     3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
     7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/* --- IMA ADPCM block format -----------------------------------------------*/
#define ADPCM_BLOCK_SIZE     1024       /* bytes per IMA ADPCM block          */
#define ADPCM_HEADER_SIZE    4          /* preamble per block                 */
#define ADPCM_DATA_PER_BLOCK (ADPCM_BLOCK_SIZE - ADPCM_HEADER_SIZE)  /* 1020 */
#define ADPCM_SAMPLES_PER_BLOCK  (1 + ADPCM_DATA_PER_BLOCK * 2)      /* 2041 */

/* --- Private state --------------------------------------------------------*/

/** DMA double-buffer – placed in normal SRAM, not on stack */
static int16_t audio_buf[AUDIO_BUF_SAMPLES];

/** ADPCM read buffer – one block */
static uint8_t adpcm_blk[ADPCM_BLOCK_SIZE];

/** Decoder state carried across blocks */
static struct {
    int16_t  predicted;
    uint8_t  step_idx;
} dec;

/** Playback context */
static struct {
    uint32_t         flash_addr;   /* base addr of ADPCM blob in W25Q128 */
    uint32_t         total_size;   /* total ADPCM bytes                  */
    uint32_t         read_offset;  /* ADPCM bytes already consumed       */
    volatile uint8_t playing;
    volatile uint8_t need_fill_0;  /* ISR flag: half 0 consumed          */
    volatile uint8_t need_fill_1;  /* ISR flag: half 1 consumed          */
    volatile uint8_t muted;        /* 1 = muted, AudioPlayer_Start/Feed no-op */
} ap;

/** Staging buffer: holds decoded PCM from one ADPCM block.
  * Because block size (2041 samples) != half-buffer size (2048 samples),
  * we must keep leftover samples between fill_half() calls. */
static struct {
    int16_t  buf[ADPCM_SAMPLES_PER_BLOCK]; /* 2041 samples max */
    uint32_t count;  /* valid samples in buf  */
    uint32_t pos;    /* next sample to read   */
} stage;

/* --- IMA ADPCM decoder ----------------------------------------------------*/

/**
  * @brief  Decode a single 4-bit ADPCM nibble to a 16-bit PCM sample
  */
static inline int16_t adpcm_decode_nibble(uint8_t nibble)
{
    int16_t step = ima_step_table[dec.step_idx];
    int32_t diff = step >> 3;

    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;

    int32_t pred = (int32_t)dec.predicted + diff;

    /* Clamp to int16 */
    if (pred > 32767)  pred = 32767;
    if (pred < -32768) pred = -32768;
    dec.predicted = (int16_t)pred;

    /* Update step index */
    int idx = (int)dec.step_idx + ima_index_table[nibble];
    if (idx < 0)  idx = 0;
    if (idx > 88) idx = 88;
    dec.step_idx = (uint8_t)idx;

    return dec.predicted;
}

/**
  * @brief  Decode one ADPCM block from adpcm_blk[] into PCM samples.
  * @param  dst        Output PCM buffer
  * @param  max_samples  Max samples to write
  * @param  blk_bytes  Actual bytes available in adpcm_blk (may be < ADPCM_BLOCK_SIZE at end)
  * @retval Number of PCM samples written
  */
static uint32_t decode_block(int16_t *dst, uint32_t max_samples, uint32_t blk_bytes)
{
    if (blk_bytes < ADPCM_HEADER_SIZE)
        return 0;

    /* Block preamble: predicted(i16 LE) + step_index(u8) + reserved(u8) */
    dec.predicted = (int16_t)((uint16_t)adpcm_blk[0] | ((uint16_t)adpcm_blk[1] << 8));
    dec.step_idx  = adpcm_blk[2];
    if (dec.step_idx > 88) dec.step_idx = 88;

    uint32_t n = 0;

    /* First sample is the predictor itself */
    if (n < max_samples)
        dst[n++] = dec.predicted;

    /* Decode nibble pairs from byte 4 onward */
    uint32_t data_bytes = blk_bytes - ADPCM_HEADER_SIZE;
    for (uint32_t i = 0; i < data_bytes && n < max_samples; i++)
    {
        uint8_t byte = adpcm_blk[ADPCM_HEADER_SIZE + i];
        dst[n++] = adpcm_decode_nibble(byte & 0x0F);        /* low nibble first */
        if (n < max_samples)
            dst[n++] = adpcm_decode_nibble((byte >> 4) & 0x0F); /* high nibble */
    }

    return n;
}

/* --- Private helpers ------------------------------------------------------*/

/**
  * @brief  Fill one half-buffer by reading + decoding ADPCM blocks from flash
  * @param  dst      Pointer to the half-buffer start
  * @param  samples  Number of PCM samples to produce
  */
static void fill_half(int16_t *dst, uint32_t samples)
{
    uint32_t written = 0;

    while (written < samples)
    {
        /* 1. Drain leftover samples from staging buffer */
        while (written < samples && stage.pos < stage.count)
            dst[written++] = stage.buf[stage.pos++];

        if (written >= samples)
            break;

        /* 2. Need more — decode next ADPCM block into staging */
        if (ap.read_offset >= ap.total_size)
            break;  /* no more ADPCM data */

        uint32_t remain = ap.total_size - ap.read_offset;
        uint32_t to_read = (remain < ADPCM_BLOCK_SIZE) ? remain : ADPCM_BLOCK_SIZE;

        W25Q128_Read(ap.flash_addr + ap.read_offset, adpcm_blk, to_read);
        ap.read_offset += to_read;

        stage.count = decode_block(stage.buf, ADPCM_SAMPLES_PER_BLOCK, to_read);
        stage.pos   = 0;
    }

    /* Pad remainder with silence */
    if (written < samples)
        memset(&dst[written], 0, (samples - written) * sizeof(int16_t));
}

/* --- Public API -----------------------------------------------------------*/

int AudioPlayer_Init(void)
{
    memset(&ap, 0, sizeof(ap));
    memset(&dec, 0, sizeof(dec));
    memset(audio_buf, 0, sizeof(audio_buf));

    /* SAI + DMA linked-list queue + linkage are all set up
       in main.c (MX_SAI1_Init, MX_SAI_Audio_Queue_Config,
       HAL_DMAEx_List_LinkQ, __HAL_LINKDMA) before ThreadX starts.
       Nothing extra to do here. */

    return 0;
}

/**
  * @brief  Diagnostic: check every step from clock to DMA readiness.
  *         Call after AudioPlayer_Init().  Returns 0 if all OK.
  *         Prints human-readable status via SWV / semihosting printf.
  */
int AudioPlayer_Check(void)
{
    int errors = 0;

    printf("[AUDIO CHECK] Start\r\n");

    /* 1. SAI1 clock enabled? */
    if (__HAL_RCC_SAI1_IS_CLK_ENABLED())
        printf("  [OK ] SAI1 clock enabled\r\n");
    else {
        printf("  [ERR] SAI1 clock NOT enabled\r\n");
        errors++;
    }

    /* 2. GPDMA1 clock enabled? */
    if (__HAL_RCC_GPDMA1_IS_CLK_ENABLED())
        printf("  [OK ] GPDMA1 clock enabled\r\n");
    else {
        printf("  [ERR] GPDMA1 clock NOT enabled\r\n");
        errors++;
    }

    /* 3. SAI handle state */
    HAL_SAI_StateTypeDef sai_state = HAL_SAI_GetState(&hsai_BlockA1);
    printf("  [INF] SAI state = 0x%02X", (unsigned)sai_state);
    if (sai_state == HAL_SAI_STATE_READY)
        printf(" (READY)\r\n");
    else if (sai_state == HAL_SAI_STATE_RESET)
    {   printf(" (RESET — not initialised!)\r\n"); errors++; }
    else
        printf("\r\n");

    /* 4. SAI slot active check */
    uint32_t slot_active = hsai_BlockA1.SlotInit.SlotActive;
    if (slot_active != 0)
        printf("  [OK ] SlotActive = 0x%08lX\r\n", (unsigned long)slot_active);
    else {
        printf("  [ERR] SlotActive = 0 (no slots enabled!)\r\n");
        errors++;
    }

    /* 5. SAI audio frequency */
    printf("  [INF] AudioFrequency = %lu\r\n",
           (unsigned long)hsai_BlockA1.Init.AudioFrequency);

    /* 6. Frame config */
    printf("  [INF] FrameLength=%lu  ActiveFrameLength=%lu  FSOffset=%lu\r\n",
           (unsigned long)hsai_BlockA1.FrameInit.FrameLength,
           (unsigned long)hsai_BlockA1.FrameInit.ActiveFrameLength,
           (unsigned long)hsai_BlockA1.FrameInit.FSOffset);

    /* 7. DMA handle linked to SAI? */
    if (hsai_BlockA1.hdmatx != NULL)
        printf("  [OK ] SAI hdmatx linked (DMA instance = 0x%08lX)\r\n",
               (unsigned long)(uintptr_t)hsai_BlockA1.hdmatx->Instance);
    else {
        printf("  [ERR] SAI hdmatx is NULL — DMA not linked!\r\n");
        errors++;
    }

    /* 8. DMA channel state */
    if (hsai_BlockA1.hdmatx != NULL)
    {
        HAL_DMA_StateTypeDef dma_state = HAL_DMA_GetState(hsai_BlockA1.hdmatx);
        printf("  [INF] DMA state = 0x%02X", (unsigned)dma_state);
        if (dma_state == HAL_DMA_STATE_READY)
            printf(" (READY)\r\n");
        else if (dma_state == HAL_DMA_STATE_RESET)
        {   printf(" (RESET — not initialised!)\r\n"); errors++; }
        else
            printf("\r\n");
    }

    /* 9. DMA linked-list check via hdmatx LinkedListQueue */
    if (hsai_BlockA1.hdmatx != NULL &&
        hsai_BlockA1.hdmatx->LinkedListQueue != NULL &&
        hsai_BlockA1.hdmatx->LinkedListQueue->Head != NULL)
        printf("  [OK ] DMA LinkedListQueue has nodes\r\n");
    else {
        printf("  [ERR] DMA LinkedListQueue is empty or not linked\r\n");
        errors++;
    }

    /* 10. Quick transmit test — send 16 silence samples (non-DMA) */
    {
        int16_t silence[16] = {0};
        HAL_StatusTypeDef tx_rc = HAL_SAI_Transmit(&hsai_BlockA1,
                                                    (uint8_t *)silence,
                                                    16, 100);
        if (tx_rc == HAL_OK)
            printf("  [OK ] SAI blocking transmit test passed\r\n");
        else {
            printf("  [ERR] SAI blocking transmit FAILED (rc=%d, err=0x%08lX)\r\n",
                   (int)tx_rc, (unsigned long)hsai_BlockA1.ErrorCode);
            errors++;
        }
    }

    /* Summary */
    if (errors == 0)
        printf("[AUDIO CHECK] All OK — ready to play\r\n");
    else
        printf("[AUDIO CHECK] FAILED — %d error(s)\r\n", errors);

    return errors;
}

int AudioPlayer_Start(uint32_t flash_addr, uint32_t adpcm_size)
{
    if (adpcm_size == 0)
        return -1;

    /* Skip playback while muted */
    if (ap.muted)
        return 0;

    /* Stop any ongoing playback first */
    AudioPlayer_Stop();

    ap.flash_addr  = flash_addr;
    ap.total_size  = adpcm_size;
    ap.read_offset = 0;
    ap.need_fill_0 = 0;
    ap.need_fill_1 = 0;

    dec.predicted = 0;
    dec.step_idx  = 0;
    stage.count   = 0;
    stage.pos     = 0;

    /* Pre-fill both halves before starting DMA */
    fill_half(&audio_buf[0],                       AUDIO_HALF_BUF_SAMPLES);
    fill_half(&audio_buf[AUDIO_HALF_BUF_SAMPLES],  AUDIO_HALF_BUF_SAMPLES);

    ap.playing = 1;

    /* Start SAI DMA in circular mode */
    if (HAL_SAI_Transmit_DMA(&hsai_BlockA1,
                             (uint8_t *)audio_buf,
                             AUDIO_BUF_SAMPLES) != HAL_OK)
    {
        ap.playing = 0;
        return -1;
    }

    return 0;
}

void AudioPlayer_Stop(void)
{
    if (ap.playing)
    {
        HAL_SAI_DMAStop(&hsai_BlockA1);
        ap.playing = 0;
    }
    ap.need_fill_0 = 0;
    ap.need_fill_1 = 0;
}

void AudioPlayer_Feed(void)
{
    if (!ap.playing || ap.muted)
        return;

    /* All ADPCM consumed → wait for both halves played, then stop */
    if (ap.read_offset >= ap.total_size)
    {
        if (ap.need_fill_0 && ap.need_fill_1)
        {
            AudioPlayer_Stop();
            return;
        }
    }

    if (ap.need_fill_0)
    {
        fill_half(&audio_buf[0], AUDIO_HALF_BUF_SAMPLES);
        ap.need_fill_0 = 0;
    }

    if (ap.need_fill_1)
    {
        fill_half(&audio_buf[AUDIO_HALF_BUF_SAMPLES], AUDIO_HALF_BUF_SAMPLES);
        ap.need_fill_1 = 0;
    }
}

uint8_t AudioPlayer_IsPlaying(void)
{
    return ap.playing;
}

/* --- SAI DMA callbacks (called from ISR) ----------------------------------*/

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_A)
        ap.need_fill_0 = 1;
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_A)
        ap.need_fill_1 = 1;
}

/* --- Mute control --------------------------------------------------------*/

void AudioPlayer_Mute(void)
{
    ap.muted = 1;
    if (ap.playing)
    {
        HAL_SAI_DMAStop(&hsai_BlockA1);
        ap.playing = 0;
    }
}

void AudioPlayer_Unmute(void)
{
    ap.muted = 0;
}

uint8_t AudioPlayer_IsMuted(void)
{
    return ap.muted;
}
