/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ssd1331.h
  * @brief   Driver header for SSD1331 OLED Display (96x64 RGB)
  ******************************************************************************
  * @attention
  *
  * SSD1331 is a 96x64 RGB OLED display driver with SPI interface
  * This driver supports 16-bit color mode (RGB565)
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __SSD1331_H__
#define __SSD1331_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"

/* SSD1331 Display Dimensions ------------------------------------------------*/
#define SSD1331_WIDTH   96
#define SSD1331_HEIGHT  64

/* SSD1331 Commands ----------------------------------------------------------*/
#define SSD1331_CMD_DRAWLINE        0x21
#define SSD1331_CMD_DRAWRECT        0x22
#define SSD1331_CMD_FILL            0x26
#define SSD1331_CMD_SETCOLUMN       0x15
#define SSD1331_CMD_SETROW          0x75
#define SSD1331_CMD_CONTRASTA       0x81
#define SSD1331_CMD_CONTRASTB       0x82
#define SSD1331_CMD_CONTRASTC       0x83
#define SSD1331_CMD_MASTERCURRENT   0x87
#define SSD1331_CMD_SETREMAP        0xA0
#define SSD1331_CMD_STARTLINE       0xA1
#define SSD1331_CMD_DISPLAYOFFSET   0xA2
#define SSD1331_CMD_NORMALDISPLAY   0xA4
#define SSD1331_CMD_DISPLAYALLON    0xA5
#define SSD1331_CMD_DISPLAYALLOFF   0xA6
#define SSD1331_CMD_INVERTDISPLAY   0xA7
#define SSD1331_CMD_SETMULTIPLEX    0xA8
#define SSD1331_CMD_SETMASTER       0xAD
#define SSD1331_CMD_DISPLAYOFF      0xAE
#define SSD1331_CMD_DISPLAYON       0xAF
#define SSD1331_CMD_POWERMODE       0xB0
#define SSD1331_CMD_PRECHARGE       0xB1
#define SSD1331_CMD_CLOCKDIV        0xB3
#define SSD1331_CMD_PRECHARGEA      0x8A
#define SSD1331_CMD_PRECHARGEB      0x8B
#define SSD1331_CMD_PRECHARGEC      0x8C
#define SSD1331_CMD_PRECHARGELEVEL  0xBB
#define SSD1331_CMD_VCOMH           0xBE
#define SSD1331_CMD_WRITERAM        0x5C  /* Enable RAM write for direct pixel data */

/* RGB565 Color Definitions --------------------------------------------------*/
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COLOR_BLACK         0x0000
#define COLOR_WHITE         0xFFFF
#define COLOR_RED           0xF800
#define COLOR_GREEN         0x07E0
#define COLOR_BLUE          0x001F
#define COLOR_CYAN          0x07FF
#define COLOR_MAGENTA       0xF81F
#define COLOR_YELLOW        0xFFE0
#define COLOR_ORANGE        0xFC00
#define COLOR_PURPLE        0x8010
#define COLOR_GRAY          0x8410
#define COLOR_DARKGRAY      0x4208
#define COLOR_LIGHTGRAY     0xC618

/* Function Prototypes -------------------------------------------------------*/

/**
  * @brief  Initialize SSD1331 OLED display
  * @retval None
  */
void SSD1331_Init(void);

/**
  * @brief  Reset SSD1331 OLED display
  * @retval None
  */
void SSD1331_Reset(void);

/**
  * @brief  Send command to SSD1331
  * @param  cmd: Command byte
  * @retval None
  */
void SSD1331_WriteCommand(uint8_t cmd);

/**
  * @brief  Send data to SSD1331
  * @param  data: Data byte
  * @retval None
  */
void SSD1331_WriteData(uint8_t data);

/**
  * @brief  Set display window
  * @param  x0: Start X coordinate
  * @param  y0: Start Y coordinate
  * @param  x1: End X coordinate
  * @param  y1: End Y coordinate
  * @retval None
  */
void SSD1331_SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);

/**
  * @brief  Fill entire screen with a color
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_FillScreen(uint16_t color);

/**
  * @brief  Draw a single pixel
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawPixel(uint8_t x, uint8_t y, uint16_t color);

/**
  * @brief  Draw a filled rectangle
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  w: Width
  * @param  h: Height
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);

/**
  * @brief  Draw a line
  * @param  x0: Start X coordinate
  * @param  y0: Start Y coordinate
  * @param  x1: End X coordinate
  * @param  y1: End Y coordinate
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t color);

/**
  * @brief  Draw a rectangle outline
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  w: Width
  * @param  h: Height
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);

/**
  * @brief  Clear the display (fill with black)
  * @retval None
  */
void SSD1331_Clear(void);

/**
  * @brief  Turn display on
  * @retval None
  */
void SSD1331_DisplayOn(void);

/**
  * @brief  Turn display off
  * @retval None
  */
void SSD1331_DisplayOff(void);

/**
  * @brief  Draw a left arrow on the display
  * @param  x: Center X coordinate
  * @param  y: Center Y coordinate
  * @param  size: Arrow size
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawLeftArrow(uint8_t x, uint8_t y, uint8_t size, uint16_t color);

/**
  * @brief  Draw a right arrow on the display
  * @param  x: Center X coordinate
  * @param  y: Center Y coordinate
  * @param  size: Arrow size
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawRightArrow(uint8_t x, uint8_t y, uint8_t size, uint16_t color);

/**
  * @brief  Draw a filled triangle
  * @param  x0, y0: First vertex
  * @param  x1, y1: Second vertex
  * @param  x2, y2: Third vertex
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_FillTriangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint16_t color);

/**
  * @brief  Start pixel streaming mode (for fast batch pixel writes)
  * @retval None
  */
void SSD1331_StartStream(void);

/**
  * @brief  Stream a single pixel (must call StartStream first)
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_StreamPixel(uint16_t color);

/**
  * @brief  End pixel streaming mode
  * @retval None
  */
void SSD1331_EndStream(void);

/**
  * @brief  Send large data buffer in chunks (optimized for DMA-safe sizes)
  * @param  data: Pointer to data buffer
  * @param  len: Length of data in bytes
  * @retval None
  */
void SSD1331_SendDataChunked(const uint8_t *data, uint32_t len);

/**
  * @brief  Draw a bitmap (RGB565 format) - optimized for animation frames
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  w: Width
  * @param  h: Height
  * @param  bitmap: Pointer to RGB565 bitmap data
  * @retval None
  */
void SSD1331_DrawBitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint16_t *bitmap);

/**
  * @brief  Draw a bitmap directly (zero-copy, fastest for animation)
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  w: Width
  * @param  h: Height
  * @param  data: Pointer to pre-formatted RGB565 data (big-endian bytes)
  * @retval None
  */
void SSD1331_DrawBitmapDirect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *data);

/**
  * @brief  Clear a window area (hardware accelerated)
  * @param  x0: Start X coordinate
  * @param  y0: Start Y coordinate
  * @param  x1: End X coordinate
  * @param  y1: End Y coordinate
  * @retval None
  */
void SSD1331_ClearWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);

/**
  * @brief  Set cursor position for pixel write
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @retval None
  */
void SSD1331_GoTo(uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif /* __SSD1331_H__ */
