/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ssd1331.c
  * @brief   Driver implementation for SSD1331 OLED Display (96x64 RGB)
  ******************************************************************************
  * @attention
  *
  * SSD1331 is a 96x64 RGB OLED display driver with SPI interface
  * This driver supports 16-bit color mode (RGB565)
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "ssd1331.h"
#include "tx_api.h"
#include <stdlib.h>

/* Private defines -----------------------------------------------------------*/
#define SSD1331_CS_LOW()    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET)
#define SSD1331_CS_HIGH()   HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET)
#define SSD1331_DC_LOW()    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET)
#define SSD1331_DC_HIGH()   HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET)
#define SSD1331_RES_LOW()   HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET)
#define SSD1331_RES_HIGH()  HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET)

/* DMA completion semaphore --------------------------------------------------*/
static TX_SEMAPHORE ssd1331_dma_sem;
static uint8_t      ssd1331_dma_sem_ready = 0;

/* Private function prototypes -----------------------------------------------*/
static void SSD1331_Delay(uint32_t ms);

/* Function implementations --------------------------------------------------*/

/**
  * @brief  Simple delay function
  * @param  ms: Delay in milliseconds
  * @retval None
  */
static void SSD1331_Delay(uint32_t ms)
{
    if (tx_thread_identify() != TX_NULL)
    {
        ULONG ticks = ms * TX_TIMER_TICKS_PER_SECOND / 1000;
        if (ticks > 0)
        {
            /* Delay spans at least one tick - yield CPU to other threads */
            tx_thread_sleep(ticks);
        }
        else
        {
            /* Sub-tick delay: busy-wait via HAL to guarantee minimum duration */
            HAL_Delay(ms);
        }
    }
    else
    {
        /* Not in a thread context (e.g. during init before scheduler) */
        HAL_Delay(ms);
    }
}

/**
  * @brief  Reset SSD1331 OLED display
  * @retval None
  */
void SSD1331_Reset(void)
{
    SSD1331_RES_HIGH();
    SSD1331_Delay(10);
    SSD1331_RES_LOW();
    SSD1331_Delay(10);
    SSD1331_RES_HIGH();
    SSD1331_Delay(10);
}

/**
  * @brief  Send command to SSD1331
  * @param  cmd: Command byte
  * @retval None
  */
void SSD1331_WriteCommand(uint8_t cmd)
{
    SSD1331_DC_LOW();   // Command mode
    SSD1331_CS_LOW();   // Select chip
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    SSD1331_CS_HIGH();  // Deselect chip
}

/**
  * @brief  Send data to SSD1331
  * @param  data: Data byte
  * @retval None
  */
void SSD1331_WriteData(uint8_t data)
{
    SSD1331_DC_HIGH();  // Data mode
    SSD1331_CS_LOW();   // Select chip
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    SSD1331_CS_HIGH();  // Deselect chip
}

/**
  * @brief  Initialize SSD1331 OLED display
  * @retval None
  */
void SSD1331_Init(void)
{
    /* Create DMA completion semaphore once */
    if (!ssd1331_dma_sem_ready) {
        tx_semaphore_create(&ssd1331_dma_sem, "SSD1331_DMA", 0);
        ssd1331_dma_sem_ready = 1;
    }

    // Reset display
    SSD1331_Reset();
    
    // Display off
    SSD1331_WriteCommand(SSD1331_CMD_DISPLAYOFF);
    
    // Set remap & data format for 16-bit RGB565 color (65K colors)
    SSD1331_WriteCommand(SSD1331_CMD_SETREMAP);
    SSD1331_WriteCommand(0x60); // RGB order, no H/V flip, COM split, 65K format 2
    
    // Set display start line
    SSD1331_WriteCommand(SSD1331_CMD_STARTLINE);
    SSD1331_WriteCommand(0x00);
    
    // Set display offset
    SSD1331_WriteCommand(SSD1331_CMD_DISPLAYOFFSET);
    SSD1331_WriteCommand(0x00);
    
    // Normal display
    SSD1331_WriteCommand(SSD1331_CMD_NORMALDISPLAY);
    
    // Set multiplex ratio
    SSD1331_WriteCommand(SSD1331_CMD_SETMULTIPLEX);
    SSD1331_WriteCommand(0x3F); // 1/64 duty
    
    // Set master configuration
    SSD1331_WriteCommand(SSD1331_CMD_SETMASTER);
    SSD1331_WriteCommand(0x8E);
    
    // Power mode
    SSD1331_WriteCommand(SSD1331_CMD_POWERMODE);
    SSD1331_WriteCommand(0x0B);
    
    // Precharge
    SSD1331_WriteCommand(SSD1331_CMD_PRECHARGE);
    SSD1331_WriteCommand(0x31);
    
    // Set clock divider
    SSD1331_WriteCommand(SSD1331_CMD_CLOCKDIV);
    SSD1331_WriteCommand(0xF0);
    
    // Precharge A
    SSD1331_WriteCommand(SSD1331_CMD_PRECHARGEA);
    SSD1331_WriteCommand(0x64);
    
    // Precharge B
    SSD1331_WriteCommand(SSD1331_CMD_PRECHARGEB);
    SSD1331_WriteCommand(0x78);
    
    // Precharge C
    SSD1331_WriteCommand(SSD1331_CMD_PRECHARGEC);
    SSD1331_WriteCommand(0x64);
    
    // Precharge level
    SSD1331_WriteCommand(SSD1331_CMD_PRECHARGELEVEL);
    SSD1331_WriteCommand(0x3A);
    
    // Set VCOMH
    SSD1331_WriteCommand(SSD1331_CMD_VCOMH);
    SSD1331_WriteCommand(0x3E);
    
    // Master current control
    SSD1331_WriteCommand(SSD1331_CMD_MASTERCURRENT);
    SSD1331_WriteCommand(0x06);
    
    // Set contrast
    SSD1331_WriteCommand(SSD1331_CMD_CONTRASTA);
    SSD1331_WriteCommand(0x91);
    SSD1331_WriteCommand(SSD1331_CMD_CONTRASTB);
    SSD1331_WriteCommand(0x50);
    SSD1331_WriteCommand(SSD1331_CMD_CONTRASTC);
    SSD1331_WriteCommand(0x7D);
    
    // Clear screen
    SSD1331_Clear();
    
    // Display on
    SSD1331_WriteCommand(SSD1331_CMD_DISPLAYON);
}

/**
  * @brief  Set display window
  * @param  x0: Start X coordinate
  * @param  y0: Start Y coordinate
  * @param  x1: End X coordinate
  * @param  y1: End Y coordinate
  * @retval None
  */
void SSD1331_SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    // Set column address
    SSD1331_WriteCommand(SSD1331_CMD_SETCOLUMN);
    SSD1331_WriteCommand(x0);
    SSD1331_WriteCommand(x1);
    
    // Set row address
    SSD1331_WriteCommand(SSD1331_CMD_SETROW);
    SSD1331_WriteCommand(y0);
    SSD1331_WriteCommand(y1);
}

/**
  * @brief  Set cursor position for pixel write
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @retval None
  */
void SSD1331_GoTo(uint8_t x, uint8_t y)
{
    SSD1331_WriteCommand(SSD1331_CMD_SETCOLUMN);
    SSD1331_WriteCommand(x);
    SSD1331_WriteCommand(x);
    SSD1331_WriteCommand(SSD1331_CMD_SETROW);
    SSD1331_WriteCommand(y);
    SSD1331_WriteCommand(y);
}

/**
  * @brief  Draw a single pixel
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawPixel(uint8_t x, uint8_t y, uint16_t color)
{
    if (x >= SSD1331_WIDTH || y >= SSD1331_HEIGHT) return;
    SSD1331_SetWindow(x, y, x, y);
    SSD1331_WriteCommand(SSD1331_CMD_WRITERAM);  // ← THÊM
    SSD1331_WriteData(color >> 8);
    SSD1331_WriteData(color & 0xFF);
}

/**
  * @brief  Fill entire screen with a color (hardware accelerated)
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_FillScreen(uint16_t color)
{
    SSD1331_FillRect(0, 0, SSD1331_WIDTH, SSD1331_HEIGHT, color);
}

/**
  * @brief  Draw a filled rectangle using hardware acceleration
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  w: Width
  * @param  h: Height
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;
    if (x >= SSD1331_WIDTH || y >= SSD1331_HEIGHT)
        return;
    
    uint8_t x1 = x + w - 1;
    uint8_t y1 = y + h - 1;
    
    if (x1 >= SSD1331_WIDTH) x1 = SSD1331_WIDTH - 1;
    if (y1 >= SSD1331_HEIGHT) y1 = SSD1331_HEIGHT - 1;
    
    // Enable fill mode
    SSD1331_WriteCommand(SSD1331_CMD_FILL);
    SSD1331_WriteCommand(0x01);  // Fill enabled
    
    // Draw rectangle command
    SSD1331_WriteCommand(SSD1331_CMD_DRAWRECT);
    SSD1331_WriteCommand(x);
    SSD1331_WriteCommand(y);
    SSD1331_WriteCommand(x1);
    SSD1331_WriteCommand(y1);
    
    // Convert RGB565 to 6-bit per channel for SSD1331
    uint8_t r6 = ((color >> 11) & 0x1F) << 1;  // 5-bit -> 6-bit
    uint8_t g6 = (color >> 5) & 0x3F;          // 6-bit (already 6-bit)
    uint8_t b6 = (color & 0x1F) << 1;          // 5-bit -> 6-bit

    // RGB mode: DRAWRECT expects R, G, B order
    // Outline color
    SSD1331_WriteCommand(r6);
    SSD1331_WriteCommand(g6);
    SSD1331_WriteCommand(b6);
    // Fill color
    SSD1331_WriteCommand(r6);
    SSD1331_WriteCommand(g6);
    SSD1331_WriteCommand(b6);
    
    // Small delay for hardware to complete
    SSD1331_Delay(1);
}

/**
  * @brief  Draw a line using hardware acceleration
  * @param  x0: Start X coordinate
  * @param  y0: Start Y coordinate
  * @param  x1: End X coordinate
  * @param  y1: End Y coordinate
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint16_t color)
{
    SSD1331_WriteCommand(SSD1331_CMD_DRAWLINE);
    SSD1331_WriteCommand(x0);
    SSD1331_WriteCommand(y0);
    SSD1331_WriteCommand(x1);
    SSD1331_WriteCommand(y1);
    // RGB mode: DRAWLINE expects R, G, B order
    SSD1331_WriteCommand(((color >> 11) & 0x1F) << 1);  // Red
    SSD1331_WriteCommand((color >> 5) & 0x3F);           // Green
    SSD1331_WriteCommand((color & 0x1F) << 1);           // Blue
}

/**
  * @brief  Draw a rectangle outline using hardware acceleration
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  w: Width
  * @param  h: Height
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    SSD1331_WriteCommand(SSD1331_CMD_FILL);
    SSD1331_WriteCommand(0x00); // No fill
    
    SSD1331_WriteCommand(SSD1331_CMD_DRAWRECT);
    SSD1331_WriteCommand(x);
    SSD1331_WriteCommand(y);
    SSD1331_WriteCommand(x + w - 1);
    SSD1331_WriteCommand(y + h - 1);
    // RGB mode: DRAWRECT expects R, G, B order
    // 6 bytes always required (outline + fill) even when fill is disabled
    SSD1331_WriteCommand(((color >> 11) & 0x1F) << 1);  // Outline Red
    SSD1331_WriteCommand((color >> 5) & 0x3F);           // Outline Green
    SSD1331_WriteCommand((color & 0x1F) << 1);           // Outline Blue
    SSD1331_WriteCommand(0x00);                          // Fill Red   (ignored)
    SSD1331_WriteCommand(0x00);                          // Fill Green (ignored)
    SSD1331_WriteCommand(0x00);                          // Fill Blue  (ignored)
}

/**
  * @brief  Clear the display (fill with black) - hardware accelerated
  * @retval None
  */
void SSD1331_Clear(void)
{
    SSD1331_ClearWindow(0, 0, SSD1331_WIDTH - 1, SSD1331_HEIGHT - 1);
}

/**
  * @brief  Turn display on
  * @retval None
  */
void SSD1331_DisplayOn(void)
{
    SSD1331_WriteCommand(SSD1331_CMD_DISPLAYON);
}

/**
  * @brief  Turn display off
  * @retval None
  */
void SSD1331_DisplayOff(void)
{
    SSD1331_WriteCommand(SSD1331_CMD_DISPLAYOFF);
}

/**
  * @brief  Swap two values
  */
static void swap_uint8(uint8_t *a, uint8_t *b)
{
    uint8_t temp = *a;
    *a = *b;
    *b = temp;
}

/**
  * @brief  Draw a filled triangle
  * @note   Simplified triangle fill using horizontal lines
  */
void SSD1331_FillTriangle(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint16_t color)
{
    int16_t a, b, y, last;
    
    // Sort vertices by Y coordinate (y0 <= y1 <= y2)
    if (y0 > y1) {
        swap_uint8(&y0, &y1);
        swap_uint8(&x0, &x1);
    }
    if (y1 > y2) {
        swap_uint8(&y2, &y1);
        swap_uint8(&x2, &x1);
    }
    if (y0 > y1) {
        swap_uint8(&y0, &y1);
        swap_uint8(&x0, &x1);
    }
    
    if (y0 == y2) { // All points on same line
        a = b = x0;
        if (x1 < a) a = x1;
        else if (x1 > b) b = x1;
        if (x2 < a) a = x2;
        else if (x2 > b) b = x2;
        SSD1331_DrawLine(a, y0, b, y0, color);
        return;
    }
    
    int16_t dx01 = x1 - x0;
    int16_t dy01 = y1 - y0;
    int16_t dx02 = x2 - x0;
    int16_t dy02 = y2 - y0;
    int16_t dx12 = x2 - x1;
    int16_t dy12 = y2 - y1;
    int32_t sa = 0;
    int32_t sb = 0;
    
    // For upper part of triangle, find scanline crossings
    if (y1 == y2) last = y1;
    else last = y1 - 1;
    
    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy02;
        b = x0 + sb / dy01;
        sa += dx02;
        sb += dx01;
        
        if (a > b) swap_uint8((uint8_t*)&a, (uint8_t*)&b);
        SSD1331_DrawLine(a, y, b, y, color);
    }
    
    // For lower part of triangle
    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        
        if (a > b) swap_uint8((uint8_t*)&a, (uint8_t*)&b);
        SSD1331_DrawLine(a, y, b, y, color);
    }
}

/**
  * @brief  Draw a left arrow (pointing left)
  * @param  x: Center X coordinate
  * @param  y: Center Y coordinate
  * @param  size: Arrow size
  * @param  color: 16-bit RGB565 color
  */
void SSD1331_DrawLeftArrow(uint8_t x, uint8_t y, uint8_t size, uint16_t color)
{
    // Draw arrow pointing left: ◄
    // Triangle pointing left + rectangle tail
    
    // Arrow head (triangle pointing left)
    uint8_t head_size = size / 2;
    SSD1331_FillTriangle(
        x - head_size, y,              // Left point
        x, y - head_size,              // Top point
        x, y + head_size,              // Bottom point
        color
    );
    
    // Arrow tail (rectangle)
    uint8_t tail_width = size / 2;
    uint8_t tail_height = size / 3;
    SSD1331_FillRect(
        x,
        y - tail_height / 2,
        tail_width,
        tail_height,
        color
    );
}

/**
  * @brief  Draw a right arrow (pointing right)
  * @param  x: Center X coordinate
  * @param  y: Center Y coordinate
  * @param  size: Arrow size
  * @param  color: 16-bit RGB565 color
  */
void SSD1331_DrawRightArrow(uint8_t x, uint8_t y, uint8_t size, uint16_t color)
{
    // Draw arrow pointing right: ►
    // Rectangle tail + triangle pointing right
    
    // Arrow tail (rectangle)
    uint8_t tail_width = size / 2;
    uint8_t tail_height = size / 3;
    SSD1331_FillRect(
        x - tail_width,
        y - tail_height / 2,
        tail_width,
        tail_height,
        color
    );
    
    // Arrow head (triangle pointing right)
    uint8_t head_size = size / 2;
    SSD1331_FillTriangle(
        x, y - head_size,              // Top point
        x + head_size, y,              // Right point
        x, y + head_size,              // Bottom point
        color
    );
}

/**
  * @brief  Start pixel streaming mode (for fast batch pixel writes)
  * @note   Call SSD1331_SetWindow() before this to define the area
  * @retval None
  */
void SSD1331_StartStream(void)
{
    SSD1331_DC_HIGH();  // Data mode
    SSD1331_CS_LOW();   // Select chip - keep selected for streaming
}

/**
  * @brief  Stream a single pixel (must call StartStream first)
  * @param  color: 16-bit RGB565 color
  * @retval None
  */
void SSD1331_StreamPixel(uint16_t color)
{
    uint8_t data[2];
    data[0] = color >> 8;
    data[1] = color & 0xFF;
    HAL_SPI_Transmit(&hspi1, data, 2, HAL_MAX_DELAY);
}

/**
  * @brief  End pixel streaming mode
  * @retval None
  */
void SSD1331_EndStream(void)
{
    SSD1331_CS_HIGH();  // Deselect chip
}

/**
  * @brief  Send large data buffer in chunks (optimized for DMA-safe sizes)
  * @param  data: Pointer to data buffer
  * @param  len: Length of data in bytes
  * @retval None
  */
void SSD1331_SendDataChunked(const uint8_t *data, uint32_t len)
{
    SSD1331_DC_HIGH();  // Data mode
    SSD1331_CS_LOW();   // Select chip

    /* Use DMA for transfers inside a ThreadX thread (len >= 16 bytes) */
    if (len >= 16 && ssd1331_dma_sem_ready && tx_thread_identify() != TX_NULL)
    {
        HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)data, (uint16_t)len);
        tx_semaphore_get(&ssd1331_dma_sem, TX_WAIT_FOREVER);
        /* CS_HIGH is handled in HAL_SPI_TxCpltCallback */
        return;
    }

    /* Polling fallback: small transfers or before RTOS scheduler starts */
    const uint32_t MAX_CHUNK = 4092;
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = (len - sent) > MAX_CHUNK ? MAX_CHUNK : (len - sent);
        HAL_SPI_Transmit(&hspi1, (uint8_t *)(data + sent), chunk, HAL_MAX_DELAY);
        sent += chunk;
    }
    SSD1331_CS_HIGH();  // Deselect chip
}

/**
  * @brief  SPI TX complete callback - signals DMA semaphore
  * @param  hspi: SPI handle
  * @retval None
  */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        SSD1331_CS_HIGH();
        tx_semaphore_put(&ssd1331_dma_sem);
    }
}

/**
  * @brief  Draw a bitmap directly (data already in big-endian byte order)
  * @param  x: X coordinate
  * @param  y: Y coordinate  
  * @param  w: Width
  * @param  h: Height
  * @param  data: Pointer to pre-formatted RGB565 data (big-endian bytes)
  * @retval None
  * @note   Zero-copy, zero-malloc - fastest method for animation
  */
void SSD1331_DrawBitmapDirect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *data)
{
    // Set window
    SSD1331_SetWindow(x, y, x + w - 1, y + h - 1);
    
    // Send data directly - no conversion needed!
    uint32_t buf_size = (uint32_t)w * h * 2;
    SSD1331_SendDataChunked(data, buf_size);
}

/**
  * @brief  Draw a bitmap (RGB565 format) - with byte order conversion
  * @param  x: X coordinate
  * @param  y: Y coordinate  
  * @param  w: Width
  * @param  h: Height
  * @param  bitmap: Pointer to RGB565 bitmap data (little-endian)
  * @retval None
  */
void SSD1331_DrawBitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint16_t *bitmap)
{
    // Set window
    SSD1331_SetWindow(x, y, x + w - 1, y + h - 1);
    
    // Convert RGB565 to big-endian byte order and send
    uint32_t pixel_count = (uint32_t)w * h;
    uint32_t buf_size = pixel_count * 2;  // 2 bytes per pixel (RGB565)
    
    uint8_t *spi_buffer = (uint8_t *)malloc(buf_size);
    if (!spi_buffer) return;
    
    // Convert to big-endian byte order for SPI
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t color = bitmap[i];
        spi_buffer[i * 2] = color >> 8;       // High byte
        spi_buffer[i * 2 + 1] = color & 0xFF; // Low byte
    }
    
    // Send entire frame in chunks
    SSD1331_SendDataChunked(spi_buffer, buf_size);
    
    free(spi_buffer);
}

/**
  * @brief  Clear a window area using hardware acceleration
  * @param  x0: Start X coordinate
  * @param  y0: Start Y coordinate
  * @param  x1: End X coordinate
  * @param  y1: End Y coordinate
  * @retval None
  */
void SSD1331_ClearWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    // Enable fill mode
    SSD1331_WriteCommand(SSD1331_CMD_FILL);
    SSD1331_WriteCommand(0x01);
    
    // Draw rectangle with black color
    SSD1331_WriteCommand(SSD1331_CMD_DRAWRECT);
    SSD1331_WriteCommand(x0);
    SSD1331_WriteCommand(y0);
    SSD1331_WriteCommand(x1);
    SSD1331_WriteCommand(y1);
    
    // Outline color (black)
    SSD1331_WriteCommand(0x00);
    SSD1331_WriteCommand(0x00);
    SSD1331_WriteCommand(0x00);
    // Fill color (black)
    SSD1331_WriteCommand(0x00);
    SSD1331_WriteCommand(0x00);
    SSD1331_WriteCommand(0x00);
    
    // Wait for hardware to complete
    SSD1331_Delay(3);
}

