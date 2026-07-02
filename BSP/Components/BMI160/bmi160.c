/**
  ******************************************************************************
  * @file    bmi160.c
  * @brief   Driver for BMI160 IMU via SPI2 — INT1 anymotion interrupt
  ******************************************************************************
  * @attention
  *
  * BMI160 connected to SPI2:
  *   PB13 → SPI2_SCK
  *   PB14 → SPI2_MISO
  *   PB15 → SPI2_MOSI
  *   PB10 → BMI160_CS  (software chip select)
  *   PB12 → BMI160_INT1 (EXTI rising edge)
  *
  * Anymotion interrupt configuration ported from proven Arduino/ESP32-S3
  * reference code (BMI160_Ref/BMI160.ino).
  *
  * Reference settings:
  *   - Accel range: +/-2g,  ODR: 25 Hz
  *   - Anymotion threshold: ~200 mg (51 LSB at 2g)
  *   - Anymotion duration:  4 consecutive samples
  *   - INT1: active-high, push-pull, 40 ms latch
  *   - Map ONLY anymotion → INT1
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "bmi160.h"

/* Private defines -----------------------------------------------------------*/
#define BMI160_CS_LOW()   HAL_GPIO_WritePin(BMI160_CS_GPIO_Port, BMI160_CS_Pin, GPIO_PIN_RESET)
#define BMI160_CS_HIGH()  HAL_GPIO_WritePin(BMI160_CS_GPIO_Port, BMI160_CS_Pin, GPIO_PIN_SET)

#define BMI160_SPI_TIMEOUT  100  /* ms */

/* ──────────────────────────────────────────────────────────────
 * Low-level SPI register access
 * ──────────────────────────────────────────────────────────────*/

void BMI160_WriteReg(uint8_t reg, uint8_t val)
{
    uint8_t txBuf[2];
    txBuf[0] = reg & 0x7F;   /* bit7 = 0 → write */
    txBuf[1] = val;

    BMI160_CS_LOW();
    HAL_SPI_Transmit(&hspi2, txBuf, 2, BMI160_SPI_TIMEOUT);
    BMI160_CS_HIGH();
}

uint8_t BMI160_ReadReg(uint8_t reg)
{
    uint8_t txBuf[2] = { reg | 0x80, 0x00 };  /* bit7 = 1 → read */
    uint8_t rxBuf[2] = { 0 };

    BMI160_CS_LOW();
    HAL_SPI_TransmitReceive(&hspi2, txBuf, rxBuf, 2, BMI160_SPI_TIMEOUT);
    BMI160_CS_HIGH();

    return rxBuf[1];
}

static void BMI160_ReadRegs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint8_t addr = reg | 0x80;

    BMI160_CS_LOW();
    HAL_SPI_Transmit(&hspi2, &addr, 1, BMI160_SPI_TIMEOUT);
    HAL_SPI_Receive(&hspi2, buf, len, BMI160_SPI_TIMEOUT);
    BMI160_CS_HIGH();
}

/* ──────────────────────────────────────────────────────────────
 * Public API
 * ──────────────────────────────────────────────────────────────*/

uint8_t BMI160_ReadChipID(void)
{
    return BMI160_ReadReg(BMI160_REG_CHIP_ID);
}

/**
  * @brief  Initialize BMI160 — anymotion interrupt on INT1 (rising edge)
  *
  * Follows the proven Arduino reference (BMI160_Ref/BMI160.ino):
  *   1. SPI mode activation (CS toggle + dummy read 0x7F)
  *   2. Soft reset → re-activate SPI
  *   3. Power up accelerometer (normal mode)
  *   4. Accel config: +/-2g, 25 Hz ODR
  *   5. Anymotion: enable X/Y only (horizontal plane), threshold ~200mg, duration 4 samples
  *   6. INT1: active-high, push-pull, output enable
  *   7. Clear all INT maps → map ONLY anymotion → INT1
  *   8. Latch: 40 ms pulse (mode 8)
  *   9. Clear pending interrupts
  *
  * @retval 0 success, negative on error
  */
int BMI160_Init(void)
{
    /* ── Step 1: SPI mode activation ────────────────────────────── */
    BMI160_CS_HIGH();
    HAL_Delay(1);
    BMI160_CS_LOW();
    HAL_Delay(1);
    BMI160_CS_HIGH();
    HAL_Delay(10);

    /* Dummy read reg 0x7F to latch SPI mode (datasheet §3.2.1) */
    BMI160_ReadReg(0x7F);
    HAL_Delay(1);

    /* ── Step 2: Soft reset ─────────────────────────────────────── */
    BMI160_WriteReg(BMI160_REG_CMD, BMI160_CMD_SOFT_RESET);
    HAL_Delay(100);

    /* Re-activate SPI mode after reset */
    BMI160_CS_HIGH();
    HAL_Delay(1);
    BMI160_CS_LOW();
    HAL_Delay(1);
    BMI160_CS_HIGH();
    HAL_Delay(10);
    BMI160_ReadReg(0x7F);
    HAL_Delay(1);

    /* Verify chip ID */
    uint8_t id = BMI160_ReadChipID();
    if (id != BMI160_CHIP_ID)
        return -1;

    /* ── Step 3: Power up accelerometer ─────────────────────────── */
    BMI160_WriteReg(BMI160_REG_CMD, BMI160_CMD_ACC_NORMAL);
    HAL_Delay(50);

    /* Wait for accel normal mode (PMU_STATUS bits [5:4] == 0b01) */
    for (int i = 0; i < 100; i++)
    {
        uint8_t pmu = BMI160_ReadReg(BMI160_REG_PMU_STATUS);
        if (((pmu >> 4) & 0x03) == 0x01)
            break;
        HAL_Delay(1);
    }

    /* ── Step 4: Accelerometer config ───────────────────────────── */
    /* +/-2g range (matching reference: BMI160.setAccelerometerRange(2)) */
    BMI160_WriteReg(BMI160_REG_ACC_RANGE, BMI160_ACC_RANGE_2G);

    /* 25 Hz ODR, normal BW (matching reference: BMI160.setAccelerometerRate(25))
       ACC_CONF = ODR[3:0]=0x06 (25Hz) | BWP[6:4]=0x02 (normal) = 0x26 */
    BMI160_WriteReg(BMI160_REG_ACC_CONF, 0x26);

    /* ── Step 5: Anymotion interrupt config ─────────────────────── */
    /* INT_MOTION_0 (0x5F): anym_dur[1:0] = 1 → 2 consecutive samples
       (more sensitive: fewer samples needed to trigger) */
    BMI160_WriteReg(BMI160_REG_INT_MOTION_0, 0x01);

    /* INT_MOTION_1 (0x60): anymotion threshold
       At +/-2g: 1 LSB = 3.91 mg, target ~78 mg → 78/3.91 = 20 = 0x14
       (more sensitive than 200 mg — detects gentle car movement) */
    BMI160_WriteReg(BMI160_REG_INT_MOTION_1, 0x14);

    /* Enable anymotion for X, Y axes only (horizontal plane)
       INT_EN_0 (0x50): bit0=X, bit1=Y, bit2=Z
       0x03 = X + Y enabled, Z disabled
       → filters out vertical vibrations (road bumps, engine) */
    BMI160_WriteReg(BMI160_REG_INT_EN_0, 0x03);

    /* ── Step 6: INT1 output config ─────────────────────────────── */
    /* INT_OUT_CTRL (0x53):
       bit0 = INT1_EDGE_CTRL (0 = level)
       bit1 = INT1_LVL       (1 = active-high)  ← our GPIO is RISING edge
       bit2 = INT1_OD        (0 = push-pull)
       bit3 = INT1_OUTPUT_EN (1 = enable)
       = 0x0A
       (reference used 0x08 active-low, but our HW uses EXTI rising → active-high) */
    BMI160_WriteReg(BMI160_REG_INT_OUT_CTRL, 0x0A);

    /* ── Step 7: Clear ALL interrupt maps, then map anymotion → INT1 */
    /* (reference: clear 0x55/0x56/0x57 first, then set 0x55 = 0x04) */
    BMI160_WriteReg(BMI160_REG_INT_MAP_0, 0x00);
    BMI160_WriteReg(BMI160_REG_INT_MAP_1, 0x00);
    BMI160_WriteReg(BMI160_REG_INT_MAP_2, 0x00);

    /* INT_MAP_0 (0x55) bit2 = int1_anymotion → map anymotion to INT1 */
    BMI160_WriteReg(BMI160_REG_INT_MAP_0, 0x04);

    /* ── Step 8: Latch mode = 40 ms pulse ───────────────────────── */
    /* INT_LATCH (0x54): mode 8 = 40 ms temporary latch
       (reference: setInterruptLatch(8)) */
    BMI160_WriteReg(BMI160_REG_INT_LATCH, 0x08);

    /* ── Step 9: Clear any pending interrupt ────────────────────── */
    BMI160_ReadReg(BMI160_REG_INT_STATUS_0);
    BMI160_ReadReg(BMI160_REG_INT_STATUS_1);
    BMI160_ReadReg(BMI160_REG_INT_STATUS_2);
    BMI160_ReadReg(BMI160_REG_INT_STATUS_3);

    return 0;
}

void BMI160_ReadAccel(BMI160_AccelData_t *data)
{
    uint8_t buf[6];
    BMI160_ReadRegs(BMI160_REG_DATA_ACC_X_L, buf, 6);
    data->x = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
    data->y = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
    data->z = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
}

void BMI160_ReadGyro(BMI160_GyroData_t *data)
{
    uint8_t buf[6];
    BMI160_ReadRegs(BMI160_REG_DATA_GYR_X_L, buf, 6);
    data->x = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
    data->y = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
    data->z = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
}

uint8_t BMI160_GetIntStatus0(void)
{
    return BMI160_ReadReg(BMI160_REG_INT_STATUS_0);
}

uint8_t BMI160_GetIntStatus2(void)
{
    return BMI160_ReadReg(BMI160_REG_INT_STATUS_2);
}

void BMI160_ClearInterrupt(void)
{
    BMI160_ReadReg(BMI160_REG_INT_STATUS_0);
    BMI160_ReadReg(BMI160_REG_INT_STATUS_1);
    BMI160_ReadReg(BMI160_REG_INT_STATUS_2);
    BMI160_ReadReg(BMI160_REG_INT_STATUS_3);
}

void BMI160_EnterLowPower(void)
{
    BMI160_WriteReg(BMI160_REG_CMD, BMI160_CMD_GYR_SUSPEND);
    HAL_Delay(30);

    /* Reduce accel ODR to 25/2 Hz for low-power anymotion */
    BMI160_WriteReg(BMI160_REG_ACC_CONF, BMI160_ACC_ODR_25_2HZ | BMI160_ACC_BWP_NORMAL);

    BMI160_WriteReg(BMI160_REG_CMD, BMI160_CMD_ACC_LOWPOWER);
    HAL_Delay(5);

    BMI160_ClearInterrupt();
}

void BMI160_ExitLowPower(void)
{
    BMI160_WriteReg(BMI160_REG_CMD, BMI160_CMD_ACC_NORMAL);
    HAL_Delay(50);

    /* Restore accel: 25 Hz, normal BW */
    BMI160_WriteReg(BMI160_REG_ACC_CONF, 0x26);

    BMI160_ClearInterrupt();
}
