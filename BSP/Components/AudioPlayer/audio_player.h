/**
  ******************************************************************************
  * @file    audio_player.h
  * @brief   Audio driver for MAX98357A via SAI1 Block A + DMA.
  *          Reads IMA-ADPCM from W25Q128, decodes to 16-bit PCM on the fly,
  *          and streams through SAI1 to the MAX98357A I2S amplifier.
  ******************************************************************************
  *
  *  Hardware:
  *    SAI1_SCK_A  → PA8   → MAX98357A BCLK
  *    SAI1_FS_A   → PB9   → MAX98357A LRCLK
  *    SAI1_SD_A   → PA10  → MAX98357A DIN
  *
  *  Audio data in W25Q128:
  *    Raw IMA-ADPCM (4-bit/sample, 16 kHz, mono).
  *    Produced by convert_audio.py, packed by pack_animations.py.
  *
  *  Playback uses SAI DMA in circular mode with a double-buffer
  *  (ping-pong). The caller must invoke AudioPlayer_Feed() periodically
  *  from a thread context so that ADPCM data is read from flash,
  *  decoded to PCM16, and written into the half of the DMA buffer
  *  that has already been consumed.
  *
  ******************************************************************************
  */

#ifndef __AUDIO_PLAYER_H__
#define __AUDIO_PLAYER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* --- Configuration --------------------------------------------------------*/
#define AUDIO_SAMPLE_RATE         16000
#define AUDIO_HALF_BUF_SAMPLES    2048          /* PCM samples per half-buf  */
#define AUDIO_BUF_SAMPLES         (AUDIO_HALF_BUF_SAMPLES * 2)

/* --- Public API -----------------------------------------------------------*/

/**
  * @brief  Initialise audio sub-system (call once at startup)
  * @retval 0 on success
  */
int AudioPlayer_Init(void);

/**
  * @brief  Start streaming IMA-ADPCM audio from external flash
  * @param  flash_addr  Absolute W25Q128 address of the raw ADPCM blob
  * @param  adpcm_size  Size of the ADPCM blob in bytes
  * @retval 0 on success, negative on error
  */
int AudioPlayer_Start(uint32_t flash_addr, uint32_t adpcm_size);

/**
  * @brief  Stop playback immediately
  */
void AudioPlayer_Stop(void);

/**
  * @brief  Refill whichever half-buffer the DMA has finished playing.
  *         Must be called from thread context (reads flash via SPI).
  *         Safe to call when not playing (no-op).
  */
void AudioPlayer_Feed(void);

/**
  * @brief  Check if audio is currently playing
  * @retval 1 = playing, 0 = idle
  */
uint8_t AudioPlayer_IsPlaying(void);

/**
  * @brief  Diagnostic: verify SAI clock, DMA link, slot config, etc.
  *         Call after AudioPlayer_Init().  Prints report via printf.
  * @retval 0 = all OK, >0 = number of errors found
  */
int AudioPlayer_Check(void);

/**
  * @brief  Mute audio — stops SAI DMA output.
  *         While muted, AudioPlayer_Start() and AudioPlayer_Feed() are no-ops.
  */
void AudioPlayer_Mute(void);

/**
  * @brief  Unmute audio — clears mute flag.
  *         Playback resumes on the next AudioPlayer_Start() call.
  */
void AudioPlayer_Unmute(void);

/**
  * @brief  Check if audio is currently muted
  * @retval 1 = muted, 0 = normal
  */
uint8_t AudioPlayer_IsMuted(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_PLAYER_H__ */
