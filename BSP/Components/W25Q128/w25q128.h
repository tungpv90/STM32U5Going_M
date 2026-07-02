/**
  ******************************************************************************
  * @file    w25q128.h
  * @brief   Driver header for W25Q128 SPI NOR Flash (16 MB)
  ******************************************************************************
  * @attention
  *
  * W25Q128 shares SPI1 bus with SSD1331 OLED display.
  * Chip select is software-managed (W25Q_CS on PA4).
  *
  * SPI1 pins:
  *   PA5 → SCK
  *   PA6 → MISO
  *   PA7 → MOSI
  *   PA4 → W25Q_CS (software CS)
  *
  ******************************************************************************
  */

#ifndef __W25Q128_H__
#define __W25Q128_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi.h"

/* W25Q128 Identification ---------------------------------------------------*/
#define W25Q128_MANUFACTURER_ID   0xEF
#define W25Q128_DEVICE_ID         0x17
#define W25Q128_JEDEC_MFR         0xEF
#define W25Q128_JEDEC_TYPE        0x40
#define W25Q128_JEDEC_CAP         0x18

/* W25Q128 Capacity ---------------------------------------------------------*/
#define W25Q128_PAGE_SIZE         256
#define W25Q128_SECTOR_SIZE       4096        /* 4 KB */
#define W25Q128_BLOCK32_SIZE      (32*1024)   /* 32 KB */
#define W25Q128_BLOCK64_SIZE      (64*1024)   /* 64 KB */
#define W25Q128_TOTAL_SIZE        (16*1024*1024) /* 16 MB */

/* W25Q128 Commands ---------------------------------------------------------*/
#define W25Q_CMD_WRITE_ENABLE     0x06
#define W25Q_CMD_WRITE_DISABLE    0x04
#define W25Q_CMD_READ_SR1         0x05
#define W25Q_CMD_READ_SR2         0x35
#define W25Q_CMD_WRITE_SR         0x01
#define W25Q_CMD_READ_DATA        0x03
#define W25Q_CMD_FAST_READ        0x0B
#define W25Q_CMD_PAGE_PROGRAM     0x02
#define W25Q_CMD_SECTOR_ERASE     0x20
#define W25Q_CMD_BLOCK32_ERASE    0x52
#define W25Q_CMD_BLOCK64_ERASE    0xD8
#define W25Q_CMD_CHIP_ERASE       0xC7
#define W25Q_CMD_POWER_DOWN       0xB9
#define W25Q_CMD_RELEASE_PD       0xAB  /* Also returns Device ID */
#define W25Q_CMD_DEVICE_ID        0xAB
#define W25Q_CMD_JEDEC_ID         0x9F
#define W25Q_CMD_MFR_DEVICE_ID    0x90

/* Status Register bits -----------------------------------------------------*/
#define W25Q_SR1_BUSY             0x01
#define W25Q_SR1_WEL              0x02

/* Function Prototypes ------------------------------------------------------*/

/**
  * @brief  Initialize W25Q128 (wake up, verify JEDEC ID)
  * @retval 0 on success, negative on failure
  */
int W25Q128_Init(void);

/**
  * @brief  Read JEDEC ID (Manufacturer + Type + Capacity)
  * @param  mfr: pointer to store manufacturer ID
  * @param  type: pointer to store memory type
  * @param  cap: pointer to store capacity
  */
void W25Q128_ReadJEDEC(uint8_t *mfr, uint8_t *type, uint8_t *cap);

/**
  * @brief  Read data from flash
  * @param  addr: 24-bit start address
  * @param  buf: destination buffer
  * @param  len: number of bytes to read
  */
void W25Q128_Read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
  * @brief  Write a page (max 256 bytes, must not cross page boundary)
  * @param  addr: 24-bit start address (should be page-aligned for best use)
  * @param  data: source buffer
  * @param  len: number of bytes (1..256)
  */
void W25Q128_WritePage(uint32_t addr, const uint8_t *data, uint16_t len);

/**
  * @brief  Write arbitrary length data (handles page boundary crossing)
  * @param  addr: 24-bit start address
  * @param  data: source buffer
  * @param  len: number of bytes
  */
void W25Q128_Write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
  * @brief  Erase a 4 KB sector
  * @param  addr: any address within the sector
  */
void W25Q128_EraseSector(uint32_t addr);

/**
  * @brief  Erase a 64 KB block
  * @param  addr: any address within the block
  */
void W25Q128_EraseBlock64(uint32_t addr);

/**
  * @brief  Erase entire chip
  */
void W25Q128_EraseChip(void);

/**
  * @brief  Wait until flash is not busy (BUSY bit cleared)
  */
void W25Q128_WaitBusy(void);

/**
  * @brief  Enter power-down mode
  */
void W25Q128_PowerDown(void);

/**
  * @brief  Wake up from power-down mode
  */
void W25Q128_WakeUp(void);

#ifdef __cplusplus
}
#endif

#endif /* __W25Q128_H__ */
