/**
  ******************************************************************************
  * @file    w25q128.c
  * @brief   Driver implementation for W25Q128 SPI NOR Flash via SPI1
  ******************************************************************************
  * @attention
  *
  * W25Q128 shares SPI1 with SSD1331. Both use software chip-select.
  *   PA5 → SPI1_SCK
  *   PA6 → SPI1_MISO
  *   PA7 → SPI1_MOSI
  *   PA4 → W25Q_CS (GPIO output, active LOW)
  *
  ******************************************************************************
  */

#include "w25q128.h"
#include <string.h>

/* Private defines -----------------------------------------------------------*/
#define W25Q_CS_LOW()   HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_RESET)
#define W25Q_CS_HIGH()  HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_SET)

#define W25Q_SPI_TIMEOUT  200  /* ms */

/* Private functions ---------------------------------------------------------*/

static uint8_t W25Q_ReadSR1(void)
{
    uint8_t tx[2] = { W25Q_CMD_READ_SR1, 0xFF };
    uint8_t rx[2];

    W25Q_CS_LOW();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();
    return rx[1];
}

static void W25Q_WriteEnable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();
}

/* Public functions ----------------------------------------------------------*/

void W25Q128_WaitBusy(void)
{
    while (W25Q_ReadSR1() & W25Q_SR1_BUSY)
    {
        /* Yield CPU if inside a ThreadX thread */
    }
}

void W25Q128_ReadJEDEC(uint8_t *mfr, uint8_t *type, uint8_t *cap)
{
    uint8_t tx[4] = { W25Q_CMD_JEDEC_ID, 0xFF, 0xFF, 0xFF };
    uint8_t rx[4];

    W25Q_CS_LOW();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 4, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();

    if (mfr)  *mfr  = rx[1];
    if (type)  *type = rx[2];
    if (cap)   *cap  = rx[3];
}

void W25Q128_WakeUp(void)
{
    uint8_t cmd = W25Q_CMD_RELEASE_PD;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();
    HAL_Delay(1);  /* tRES1 = 3 µs, 1 ms is safe */
}

void W25Q128_PowerDown(void)
{
    uint8_t cmd = W25Q_CMD_POWER_DOWN;

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();
}

int W25Q128_Init(void)
{
    /* Ensure CS is high */
    W25Q_CS_HIGH();
    HAL_Delay(10);

    /* Wake up from power-down (in case it was sleeping) */
    W25Q128_WakeUp();

    /* Verify JEDEC ID */
    uint8_t mfr, type, cap;
    W25Q128_ReadJEDEC(&mfr, &type, &cap);

    if (mfr != W25Q128_JEDEC_MFR || type != W25Q128_JEDEC_TYPE || cap != W25Q128_JEDEC_CAP)
        return -1;  /* Wrong or no chip */

    return 0;
}

void W25Q128_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    /*
     * SPI1 is shared with SSD1331: HAL_SPI_Transmit sets COMM=TX-only and
     * SPI_CloseTransfer does NOT flush RXFIFO nor clear OVR in that mode.
     * Force a clean SPI1 state before the flash transaction.
     */
    if (hspi1.State != HAL_SPI_STATE_READY)
    {
        HAL_SPI_Abort(&hspi1);
    }
    HAL_SPIEx_FlushRxFifo(&hspi1);
    __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
    __HAL_SPI_CLEAR_EOTFLAG(&hspi1);
    __HAL_SPI_CLEAR_TXTFFLAG(&hspi1);
    hspi1.State = HAL_SPI_STATE_READY;
    hspi1.ErrorCode = HAL_SPI_ERROR_NONE;

    /*
     * W25Q128 SPI read: 4-byte command + continuous data, all within one
     * CS-LOW session.  On STM32U5 SPI v2, mixing HAL_SPI_Transmit /
     * HAL_SPI_Receive causes SPI disable/re-enable between calls which
     * breaks the flash transaction.
     *
     * Solution: use HAL_SPI_TransmitReceive for the entire operation.
     * TX sends cmd bytes then 0xFF padding; RX ignores first 4 bytes
     * (command phase) then captures the real flash data.
     */

    /* Small reads (≤252 bytes): single TransmitReceive on stack buffer */
    #define W25Q_SMALL_READ_MAX  256  /* 4 cmd + 252 data */

    if (len <= (W25Q_SMALL_READ_MAX - 4))
    {
        uint16_t total = 4 + (uint16_t)len;
        uint8_t tx_buf[W25Q_SMALL_READ_MAX];
        uint8_t rx_buf[W25Q_SMALL_READ_MAX];

        tx_buf[0] = W25Q_CMD_READ_DATA;
        tx_buf[1] = (addr >> 16) & 0xFF;
        tx_buf[2] = (addr >> 8)  & 0xFF;
        tx_buf[3] =  addr        & 0xFF;
        memset(&tx_buf[4], 0xFF, len);  /* dummy bytes to clock out data */

        W25Q_CS_LOW();
        HAL_SPI_TransmitReceive(&hspi1, tx_buf, rx_buf, total, W25Q_SPI_TIMEOUT);
        W25Q_CS_HIGH();

        memcpy(buf, &rx_buf[4], len);   /* skip 4 command-phase bytes */
        return;
    }

    /* Large reads: chunked TransmitReceive */
    {
        uint8_t cmd_tx[4] = {
            W25Q_CMD_READ_DATA,
            (addr >> 16) & 0xFF,
            (addr >>  8) & 0xFF,
             addr        & 0xFF
        };
        uint8_t cmd_rx[4];

        W25Q_CS_LOW();

        /* Command phase — full-duplex keeps SPI enabled throughout */
        HAL_SPI_TransmitReceive(&hspi1, cmd_tx, cmd_rx, 4, W25Q_SPI_TIMEOUT);

        /* Data phase — clock out 0xFF, read flash data.
         * Use TransmitReceive with 0xFF padding to stay in full-duplex
         * (COMM=00) and avoid the SPI disable/re-enable that
         * HAL_SPI_Receive would cause by switching to COMM=RX-only.
         */
        uint8_t pad[256];
        memset(pad, 0xFF, sizeof(pad));

        while (len > 0)
        {
            uint16_t chunk = (len > sizeof(pad)) ? sizeof(pad) : (uint16_t)len;
            HAL_SPI_TransmitReceive(&hspi1, pad, buf, chunk, W25Q_SPI_TIMEOUT);
            buf += chunk;
            len -= chunk;
        }

        W25Q_CS_HIGH();
    }
}

void W25Q128_WritePage(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > W25Q128_PAGE_SIZE) return;

    uint8_t cmd[4];
    cmd[0] = W25Q_CMD_PAGE_PROGRAM;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, cmd, 4, W25Q_SPI_TIMEOUT);
    HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();

    W25Q128_WaitBusy();
}

void W25Q128_Write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len > 0)
    {
        /* Bytes remaining in current page */
        uint32_t page_remain = W25Q128_PAGE_SIZE - (addr % W25Q128_PAGE_SIZE);
        uint32_t to_write = (len < page_remain) ? len : page_remain;

        W25Q128_WritePage(addr, data, (uint16_t)to_write);

        addr += to_write;
        data += to_write;
        len  -= to_write;
    }
}

void W25Q128_EraseSector(uint32_t addr)
{
    uint8_t cmd[4];
    cmd[0] = W25Q_CMD_SECTOR_ERASE;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, cmd, 4, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();

    W25Q128_WaitBusy();
}

void W25Q128_EraseBlock64(uint32_t addr)
{
    uint8_t cmd[4];
    cmd[0] = W25Q_CMD_BLOCK64_ERASE;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, cmd, 4, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();

    W25Q128_WaitBusy();
}

void W25Q128_EraseChip(void)
{
    uint8_t cmd = W25Q_CMD_CHIP_ERASE;

    W25Q_WriteEnable();

    W25Q_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, W25Q_SPI_TIMEOUT);
    W25Q_CS_HIGH();

    W25Q128_WaitBusy();
}
