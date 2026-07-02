/**
  ******************************************************************************
  * @file    bmi160.h
  * @brief   Driver header for BMI160 IMU (Accelerometer + Gyroscope)
  ******************************************************************************
  * @attention
  *
  * BMI160 communicates via SPI2. Chip select is software-managed (BMI160_CS).
  * INT1 pin (PB12) generates EXTI rising-edge interrupt on motion detection.
  *
  * Anymotion interrupt configuration derived from proven Arduino/ESP32-S3
  * reference (BMI160_Ref).
  *
  ******************************************************************************
  */

#ifndef __BMI160_H__
#define __BMI160_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"

/* BMI160 Chip ID ------------------------------------------------------------*/
#define BMI160_CHIP_ID              0xD1

/* BMI160 Register Addresses -------------------------------------------------*/
#define BMI160_REG_CHIP_ID          0x00
#define BMI160_REG_ERR_REG          0x02
#define BMI160_REG_PMU_STATUS       0x03
#define BMI160_REG_DATA_MAG_X_L     0x04
#define BMI160_REG_DATA_GYR_X_L     0x0C
#define BMI160_REG_DATA_ACC_X_L     0x12
#define BMI160_REG_STATUS           0x1B
#define BMI160_REG_INT_STATUS_0     0x1C
#define BMI160_REG_INT_STATUS_1     0x1D
#define BMI160_REG_INT_STATUS_2     0x1E
#define BMI160_REG_INT_STATUS_3     0x1F
#define BMI160_REG_ACC_CONF         0x40
#define BMI160_REG_ACC_RANGE        0x41
#define BMI160_REG_GYR_CONF         0x42
#define BMI160_REG_GYR_RANGE        0x43
#define BMI160_REG_INT_EN_0         0x50
#define BMI160_REG_INT_EN_1         0x51
#define BMI160_REG_INT_EN_2         0x52
#define BMI160_REG_INT_OUT_CTRL     0x53
#define BMI160_REG_INT_LATCH        0x54
#define BMI160_REG_INT_MAP_0        0x55
#define BMI160_REG_INT_MAP_1        0x56
#define BMI160_REG_INT_MAP_2        0x57
#define BMI160_REG_INT_MOTION_0     0x5F
#define BMI160_REG_INT_MOTION_1     0x60
#define BMI160_REG_INT_MOTION_2     0x61
#define BMI160_REG_INT_MOTION_3     0x62
#define BMI160_REG_CMD              0x7E

/* BMI160 Commands -----------------------------------------------------------*/
#define BMI160_CMD_SOFT_RESET       0xB6
#define BMI160_CMD_ACC_NORMAL       0x11
#define BMI160_CMD_ACC_LOWPOWER     0x12
#define BMI160_CMD_GYR_NORMAL       0x15
#define BMI160_CMD_ACC_SUSPEND      0x10
#define BMI160_CMD_GYR_SUSPEND      0x14
#define BMI160_CMD_INT_RESET        0xB1

/* Accelerometer Range -------------------------------------------------------*/
#define BMI160_ACC_RANGE_2G         0x03
#define BMI160_ACC_RANGE_4G         0x05
#define BMI160_ACC_RANGE_8G         0x08
#define BMI160_ACC_RANGE_16G        0x0C

/* Accelerometer ODR (bits [3:0] of ACC_CONF) --------------------------------*/
#define BMI160_ACC_ODR_25_2HZ       0x05
#define BMI160_ACC_ODR_25HZ         0x06
#define BMI160_ACC_ODR_50HZ         0x07
#define BMI160_ACC_ODR_100HZ        0x08
#define BMI160_ACC_ODR_200HZ        0x09

/* Accelerometer BWP (bits [6:4] of ACC_CONF) --------------------------------*/
#define BMI160_ACC_BWP_NORMAL       0x20  /* normal filter, OSR4 */

/* INT_STATUS_0 (0x1C) bit masks ---------------------------------------------*/
#define BMI160_ANYMOTION_INT        0x04  /* bit2 = anym_int */

/* INT_STATUS_2 (0x1E) bit masks — anymotion axis/sign ----------------------*/
#define BMI160_ANYM_SIGN_BIT        0x08
#define BMI160_ANYM_1ST_Z_BIT       0x04
#define BMI160_ANYM_1ST_Y_BIT       0x02
#define BMI160_ANYM_1ST_X_BIT       0x01

/* Data structures -----------------------------------------------------------*/
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} BMI160_AccelData_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} BMI160_GyroData_t;

/* Function Prototypes -------------------------------------------------------*/

int  BMI160_Init(void);
uint8_t BMI160_ReadChipID(void);
uint8_t BMI160_ReadReg(uint8_t reg);
void BMI160_WriteReg(uint8_t reg, uint8_t val);
void BMI160_ReadAccel(BMI160_AccelData_t *data);
void BMI160_ReadGyro(BMI160_GyroData_t *data);
uint8_t BMI160_GetIntStatus0(void);
uint8_t BMI160_GetIntStatus2(void);
void BMI160_ClearInterrupt(void);
void BMI160_EnterLowPower(void);
void BMI160_ExitLowPower(void);

#ifdef __cplusplus
}
#endif

#endif /* __BMI160_H__ */
