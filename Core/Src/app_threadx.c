/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
  ******************************************************************************
    * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"
#include "ssd1331.h"
#include "spi.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define OLED_THREAD_STACK_SIZE    4096
#define OLED_THREAD_PRIORITY      6
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static TX_THREAD oled_thread;
static UCHAR oled_thread_stack[OLED_THREAD_STACK_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID oled_thread_entry(ULONG initial_input);
/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  /* USER CODE BEGIN App_ThreadX_MEM_POOL */

  /* USER CODE END App_ThreadX_MEM_POOL */
  /* USER CODE BEGIN App_ThreadX_Init */

  ret = tx_thread_create(&oled_thread, "OLED Thread", oled_thread_entry, 0,
                         oled_thread_stack, OLED_THREAD_STACK_SIZE,
                         OLED_THREAD_PRIORITY, OLED_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE, TX_AUTO_START);

  /* USER CODE END App_ThreadX_Init */

  return ret;
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

/* ────────────────────────────────────────────────────────────────────────
 * OLED HARDWARE DIAGNOSTIC
 * ────────────────────────────────────────────────────────────────────────
 * LED (PC13) status codes:
 *   3 quick blinks .............. thread started
 *   Slow 1 Hz blink ............. phase 1: toggling OLED control pins
 *                                 (measure PB0=RES, PB1=DC, PB2=CS with DMM
 *                                  — should show ~1.65 V average)
 *   Fast 5 Hz blink ............. phase 2: continuous SPI transmit
 *                                 (scope PA5=SCK, PA7=MOSI — expect square
 *                                  wave clock and 0x55 data pattern)
 *   Solid ON .................... phase 3: SSD1331_Init + FillScreen returned;
 *                                 if OLED still dark → hardware/wiring issue
 *   SOS pattern (··· − − − ···).. HAL_SPI_Transmit returned an error
 * ──────────────────────────────────────────────────────────────────────── */

#define DIAG_TICKS_MS(ms) ((ULONG)(((uint64_t)(ms) * TX_TIMER_TICKS_PER_SECOND + 999) / 1000))

static void diag_led_blink(uint32_t times, uint32_t period_ms)
{
    for (uint32_t i = 0; i < times; i++)
    {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        tx_thread_sleep(DIAG_TICKS_MS(period_ms / 2));
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        tx_thread_sleep(DIAG_TICKS_MS(period_ms / 2));
    }
}

static void diag_sos_forever(void)
{
    while (1)
    {
        /* S: three short */
        diag_led_blink(3, 200);
        tx_thread_sleep(DIAG_TICKS_MS(300));
        /* O: three long */
        for (int i = 0; i < 3; i++) {
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            tx_thread_sleep(DIAG_TICKS_MS(500));
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            tx_thread_sleep(DIAG_TICKS_MS(200));
        }
        tx_thread_sleep(DIAG_TICKS_MS(300));
        /* S: three short */
        diag_led_blink(3, 200);
        tx_thread_sleep(DIAG_TICKS_MS(1500));
    }
}

static VOID oled_thread_entry(ULONG initial_input)
{
    (VOID)initial_input;

    /* ── Startup marker: 3 quick blinks ──────────────────────────────── */
    diag_led_blink(3, 150);
    tx_thread_sleep(DIAG_TICKS_MS(500));

    /* ── Phase 1: Toggle control pins so DMM/scope can verify they wiggle
     *   Also proves GPIO clock enable + pin config for PB0/PB1/PB2 works.
     *   LED slow-blinks at 1 Hz throughout.
     * ──────────────────────────────────────────────────────────────── */
    for (int i = 0; i < 6; i++)
    {
        HAL_GPIO_TogglePin(OLED_RES_GPIO_Port, OLED_RES_Pin);
        HAL_GPIO_TogglePin(OLED_DC_GPIO_Port,  OLED_DC_Pin);
        HAL_GPIO_TogglePin(OLED_CS_GPIO_Port,  OLED_CS_Pin);
        HAL_GPIO_TogglePin(LED_GPIO_Port,      LED_Pin);
        tx_thread_sleep(DIAG_TICKS_MS(500));
    }
    /* Leave RES/CS high (idle), DC low */
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port,  OLED_CS_Pin,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port,  OLED_DC_Pin,  GPIO_PIN_RESET);

    /* ── Phase 2: Bang SPI1 with 0x55 continuously for a few seconds
     *   User scopes PA5 (SCK) and PA7 (MOSI). Expect a clean square-wave
     *   clock while CS is LOW and 0x55 = 0101 0101 on MOSI.
     *   LED fast-blinks at 5 Hz during this.
     * ──────────────────────────────────────────────────────────────── */
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
    uint8_t pattern = 0x55;
    ULONG   start   = tx_time_get();
    ULONG   dur     = DIAG_TICKS_MS(3000);
    ULONG   next_led_toggle = start;
    while ((tx_time_get() - start) < dur)
    {
        HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, &pattern, 1, 100);
        if (st != HAL_OK)
        {
            HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
            diag_sos_forever();  /* Never returns */
        }
        if ((tx_time_get() - next_led_toggle) >= DIAG_TICKS_MS(100))
        {
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            next_led_toggle = tx_time_get();
        }
    }
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_GPIO_Port,     LED_Pin,     GPIO_PIN_RESET);
    tx_thread_sleep(DIAG_TICKS_MS(500));

    /* ── Phase 3: Full SSD1331 init + color cycle ───────────────────── */
    SSD1331_Init();
    SSD1331_DisplayOn();

    /* Solid LED = init returned; if screen still dark, it's a hardware
       (wiring / power / VCC ~ VBAT missing) issue, not code. */
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

    static const uint16_t colors[] = {
        COLOR_RED, COLOR_GREEN, COLOR_BLUE,
        COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA, COLOR_WHITE,
    };
    const uint32_t n = sizeof(colors) / sizeof(colors[0]);
    uint32_t idx = 0;

    while (1)
    {
        SSD1331_FillScreen(colors[idx]);
        /* LED heartbeat so we know thread still running */
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
        idx = (idx + 1) % n;
    }
}
/* USER CODE END 1 */
