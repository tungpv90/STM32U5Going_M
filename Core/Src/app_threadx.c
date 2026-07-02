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
#include "bmi160.h"
#include "w25q128.h"
#include "animation_player.h"
#include "audio_player.h"
#include "button_handler.h"
#include "gpio.h"
#include "gpdma.h"
#include "spi.h"
#include "sai.h"
#include "linked_list.h"
#include "icache.h"
#include "stm32u5xx_hal_pwr_ex.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define OLED_THREAD_STACK_SIZE    4096
#define OLED_THREAD_PRIORITY      6
#define LED_THREAD_STACK_SIZE     512
#define LED_THREAD_PRIORITY       10
#define MOTION_THREAD_STACK_SIZE  1024
#define MOTION_THREAD_PRIORITY    8
#define BUTTON_THREAD_STACK_SIZE  1024
#define BUTTON_THREAD_PRIORITY    5

#define DEEP_SLEEP_TIMEOUT_SEC    15  /* Enter deep sleep after 15s of no motion (was 60s) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static TX_THREAD oled_thread;
static UCHAR oled_thread_stack[OLED_THREAD_STACK_SIZE];
static TX_THREAD led_thread;
static UCHAR led_thread_stack[LED_THREAD_STACK_SIZE];
static TX_THREAD motion_thread;
static UCHAR motion_thread_stack[MOTION_THREAD_STACK_SIZE];
static TX_THREAD button_thread;
static UCHAR button_thread_stack[BUTTON_THREAD_STACK_SIZE];
static TX_SEMAPHORE motion_sem;

/* Animation player instance */
static AnimPlayer_t anim_player;

/* Deep sleep state: 1 = system sleeping, 0 = active */
static volatile uint8_t system_sleeping = 0;

/* Set by button_thread after wake — tells oled_thread to restart cleanly */
static volatile uint8_t oled_restart_pending = 0;

/* Feature toggle flags */
static volatile uint8_t gyro_enabled       = 0;  /* 0 = gyro off, 1 = gyro on  */
static volatile uint8_t sleep_mode_enabled = 1;  /* 1 = auto-sleep active       */
static volatile uint8_t battery_showing    = 0;  /* 1 = battery icon on screen  */

/* Auto-sleep watchdog: last user-activity timestamp (ThreadX ticks) */
static volatile ULONG last_activity_tick = 0;

/* Set by motion_thread before auto-sleep so button_thread ignores the
   wake-up press (prevents immediate re-sleep). */
static volatile uint8_t ignore_wake_press = 0;

/* Wake source after STOP2: 0 = button (EXTI0), 1 = BMI160 motion (EXTI12) */
static volatile uint8_t woke_from_motion = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID oled_thread_entry(ULONG initial_input);
static VOID led_thread_entry(ULONG initial_input);
static VOID motion_thread_entry(ULONG initial_input);
static VOID button_thread_entry(ULONG initial_input);
static void System_EnterDeepSleep(void);
static void show_battery_icon(void);
static inline void reset_activity(void);
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

  if (ret == TX_SUCCESS)
  {
    ret = tx_thread_create(&led_thread, "LED Thread", led_thread_entry, 0,
                           led_thread_stack, LED_THREAD_STACK_SIZE,
                           LED_THREAD_PRIORITY, LED_THREAD_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
  }

  if (ret == TX_SUCCESS)
  {
    ret = tx_semaphore_create(&motion_sem, "MotionSem", 0);
  }

  if (ret == TX_SUCCESS)
  {
    ret = tx_thread_create(&motion_thread, "Motion Thread", motion_thread_entry, 0,
                           motion_thread_stack, MOTION_THREAD_STACK_SIZE,
                           MOTION_THREAD_PRIORITY, MOTION_THREAD_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
  }

  if (ret == TX_SUCCESS)
  {
    ret = ButtonHandler_Init();
  }

  if (ret == TX_SUCCESS)
  {
    ret = tx_thread_create(&button_thread, "Button Thread", button_thread_entry, 0,
                           button_thread_stack, BUTTON_THREAD_STACK_SIZE,
                           BUTTON_THREAD_PRIORITY, BUTTON_THREAD_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
  }

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

/* ──────────────────────────────────────────────────────────────────────────
 * True STOP2 Deep Sleep — consumes ~3-5 µA.
 *
 * Strategy:
 *   1. Signal threads to idle (system_sleeping = 1) and wait for them
 *   2. Shut down ALL external peripherals (OLED, flash, audio)
 *   3. De-init MCU peripherals (SPI, SAI, DMA)
 *      NOTE: BMI160 stays in low-power mode with anymotion INT1 active
 *   4. Set GPIO to analog EXCEPT PA0 (button) and PB12 (BMI160_INT1)
 *   5. Disable SysTick (ThreadX) and TIM6 (HAL tick)
 *   6. Clear all NVIC pending, enable EXTI0 + EXTI12, enter STOP2 via WFI
 *   — MCU sleeps here, consuming ~3-5 µA —
 *   7. EXTI0 (button) OR EXTI12 (BMI160 motion) wakes MCU
 *   8. Restore system clock (PLL 160 MHz)
 *   9. Re-enable SysTick and HAL tick
 *  10. Re-init all MCU peripherals (GPIO, DMA, SPI, SAI)
 *  11. Re-init all BSP drivers (flash, OLED, IMU, audio, animation)
 *  12. Clear sleeping flag → threads resume
 * ──────────────────────────────────────────────────────────────────────────*/

extern SAI_HandleTypeDef hsai_BlockA1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel2;
extern DMA_QListTypeDef SAI_Audio_Queue;
extern void SystemClock_Config(void);

/**
  * @brief  Enter true STOP2 deep sleep.  Blocks until button or BMI160 motion
  *         wakes the MCU.  Called from motion_thread (auto-sleep timeout) or
  *         button_thread (manual sleep via single press).
  *
  *         Wake sources:
  *           - EXTI0  (PA0)  = button press
  *           - EXTI12 (PB12) = BMI160 anymotion interrupt (INT1)
  */
static void System_EnterDeepSleep(void)
{
    /* ── Phase 1: Signal threads to idle and let them settle ────────── */
    system_sleeping = 1;
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 2);  /* 500 ms */

    /* ── Phase 2: Stop application-level activity ───────────────────── */
    AnimPlayer_Stop(&anim_player);
    AudioPlayer_Stop();

    /* ── Phase 3: Power down external hardware ──────────────────────── */
    SSD1331_DisplayOff();
    W25Q128_PowerDown();
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    BMI160_EnterLowPower();

    /* ── Phase 4: De-init MCU peripherals ───────────────────────────── */
    HAL_SAI_DMAStop(&hsai_BlockA1);
    HAL_SAI_DeInit(&hsai_BlockA1);
    HAL_SPI_DeInit(&hspi1);
    HAL_SPI_DeInit(&hspi2);

    /* De-init DMA channels to prevent residual activity */
    HAL_DMA_DeInit(&handle_GPDMA1_Channel0);
    HAL_DMA_DeInit(&handle_GPDMA1_Channel1);
    HAL_DMA_DeInit(&handle_GPDMA1_Channel2);

    /* Disable ICACHE before entering STOP2 */
    HAL_ICACHE_Disable();

    /* ── Phase 5: Set all GPIO to analog except PA0 (wake button) ──── */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;

    /* GPIOC: PC13 (LED) */
    gpio.Pin = LED_Pin;
    HAL_GPIO_Init(LED_GPIO_Port, &gpio);

    /* GPIOB: OLED_RES(PB0), OLED_DC(PB1), OLED_CS(PB2),
              BMI160_CS(PB10), SAI1_FS_A(PB9),
              SPI2_SCK(PB13), SPI2_MISO(PB14), SPI2_MOSI(PB15)
       NOTE: BMI160_INT1(PB12) is NOT set to analog — it stays as
             EXTI rising-edge input so BMI160 motion can wake from STOP2. */
    gpio.Pin = OLED_RES_Pin | OLED_DC_Pin | OLED_CS_Pin
             | BMI160_CS_Pin
             | GPIO_PIN_9 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* GPIOA: W25Q_CS(PA4), SPI1_SCK(PA5), SPI1_MISO(PA6), SPI1_MOSI(PA7),
              SAI1_SCK_A(PA8), SAI1_SD_A(PA10)
       DO NOT touch PA0 (button EXTI) */
    gpio.Pin = W25Q_CS_Pin | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7
             | GPIO_PIN_8 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* NOTE: SRAM1-3 retention MUST stay enabled in STOP2 because the CPU
       resumes execution in-place after WFI — stack, globals, and ThreadX
       kernel state all live in SRAM and must survive. */

    /* ── Phase 6: Save & disable tick sources ───────────────────────── */
    ULONG saved_systick_load = SysTick->LOAD;
    SysTick->CTRL = 0;              /* Disable SysTick completely        */
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;  /* Clear SysTick pending      */
    HAL_SuspendTick();               /* Disable TIM6 update interrupt    */

    /* ── Phase 7: Enter STOP2 ───────────────────────────────────────── */
    __disable_irq();

    /* Clear ALL pending NVIC interrupts */
    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;   /* Disable all NVIC interrupts   */
        NVIC->ICPR[i] = 0xFFFFFFFF;   /* Clear all pending              */
    }

    /* Re-enable EXTI0 (button) AND EXTI12 (BMI160 INT1) for wakeup */
    __HAL_GPIO_EXTI_CLEAR_FLAG(BTN_USER_Pin);
    HAL_NVIC_SetPriority(EXTI0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    __HAL_GPIO_EXTI_CLEAR_FLAG(BMI160_INT1_Pin);
    HAL_NVIC_SetPriority(EXTI12_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI12_IRQn);

    /* Enter STOP2 — CPU sleeps here until EXTI0 or EXTI12 fires */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* ═══════ MCU WAKES UP HERE ═══════ */
    /* Clock is now MSI ~4 MHz, all PLL/HSI settings lost */
    /* Determine wake source: check EXTI pending flags before clearing */

    __enable_irq();

    /* Detect which EXTI line woke us */
    if (__HAL_GPIO_EXTI_GET_FLAG(BMI160_INT1_Pin))
        woke_from_motion = 1;
    else
        woke_from_motion = 0;

    /* ── Phase 8: Restore system clock to PLL 160 MHz ──────────────── */
    SystemClock_Config();

    /* ── Phase 9: Restore tick sources ──────────────────────────────── */
    HAL_ResumeTick();                /* Re-enable TIM6 */
    HAL_InitTick(TICK_INT_PRIORITY); /* Re-configure TIM6 for new clock  */

    /* Restore SysTick for ThreadX */
    SysTick->LOAD = saved_systick_load;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk
                  | SysTick_CTRL_TICKINT_Msk
                  | SysTick_CTRL_ENABLE_Msk;

    /* ── Phase 10: Re-init all MCU peripherals ─────────────────────── */
    MX_GPIO_Init();
    MX_GPDMA1_Init();
    MX_ICACHE_Init();
    MX_SPI1_Init();
    MX_SPI2_Init();
    MX_SAI1_Init();

    /* Re-link SAI DMA chain */
    MX_SAI_Audio_Queue_Config();
    HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel2, &SAI_Audio_Queue);
    __HAL_LINKDMA(&hsai_BlockA1, hdmatx, handle_GPDMA1_Channel2);

    /* ── Phase 11: Re-init BSP components ──────────────────────────── */
    W25Q128_WakeUp();
    W25Q128_Init();
    SSD1331_Init();
    SSD1331_FillScreen(COLOR_BLACK);
    BMI160_ExitLowPower();
    BMI160_ClearInterrupt();  /* Clear any latched motion interrupt */
    AudioPlayer_Init();
    AnimPlayer_Init(&anim_player, 0x00000000);

    /* Drain any stale motion semaphore counts from the wake-up edge */
    while (tx_semaphore_get(&motion_sem, TX_NO_WAIT) == TX_SUCCESS) {}

    /* ── Phase 12: Wake threads ────────────────────────────────────── */
    system_sleeping = 0;
    oled_restart_pending = 1;
    reset_activity();  /* restart auto-sleep countdown */

    /* If woke by motion, set ignore_wake_press so button_thread doesn't
       misinterpret EXTI0 noise as a button press. */
    if (woke_from_motion)
        ignore_wake_press = 1;
}

/* ──────────────────────────────────────────────────────────────────────────
 * Activity tracking for auto-sleep
 * ──────────────────────────────────────────────────────────────────────────*/

static inline void reset_activity(void)
{
    last_activity_tick = tx_time_get();
}

/* ──────────────────────────────────────────────────────────────────────────
 * Thread entries
 * ──────────────────────────────────────────────────────────────────────────*/

static VOID led_thread_entry(ULONG initial_input)
{
    (VOID)initial_input;

    while (1)
    {
        /* Idle when system is off */
        if (system_sleeping)
        {
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 4);
            continue;
        }

        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);   /* LED ON  */
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);               /* 1s      */
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); /* LED OFF */
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);               /* 1s      */
    }
}

static VOID oled_thread_entry(ULONG initial_input)
{
    (VOID)initial_input;

    /* Initialize OLED display */
    SSD1331_Init();
    SSD1331_FillScreen(COLOR_BLACK);

    /* Start auto-sleep countdown from boot */
    reset_activity();

    /* ──────────── W25Q128 Init ──────────── */
    W25Q128_WakeUp();

    if (W25Q128_Init() != 0)
    {
        while (1) { tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND); }
    }

    /* ──────────── BMI160 Init ──────────── */
    if (BMI160_Init() != 0)
    {
        while (1) { tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND); }
    }

    /* ──────────── AnimPlayer Init ──────────── */
    if (AnimPlayer_Init(&anim_player, 0x00000000) != 0)
    {
        while (1) { tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND); }
    }

    /* ──────────── AudioPlayer Init ──────────── */
    if (AudioPlayer_Init() != 0)
    {
        while (1) { tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND); }
    }

    /* ──────────── Audio Diagnostic ──────────── */
    AudioPlayer_Check();

    /* ──────────── Cycle all animations ──────────── */
    SSD1331_FillScreen(COLOR_BLACK);

    uint16_t anim_total = AnimPlayer_GetCount(&anim_player);
    uint16_t anim_cur   = 0;

    while (1)
    {
        /* ── Idle when system is off — no peripheral access! ── */
        if (system_sleeping)
        {
            tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 4);
            continue;
        }

        /* ── Pause while battery icon is displayed ── */
        if (battery_showing)
        {
            tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 10);
            continue;
        }

        /* ── After power-on, restart from clean state ── */
        if (oled_restart_pending)
        {
            oled_restart_pending = 0;
            anim_total = AnimPlayer_GetCount(&anim_player);
            anim_cur   = 0;
            SSD1331_FillScreen(COLOR_BLACK);
        }

        if (AnimPlayer_Select(&anim_player, anim_cur) != 0)
        {
            anim_cur = (anim_cur + 1) % anim_total;
            continue;
        }

        anim_player.loop = 0;
        anim_player.fps  = ANIM_DEFAULT_FPS;
        AnimPlayer_Play(&anim_player);

        anim_cur = (anim_cur + 1) % anim_total;
    }
}

static VOID motion_thread_entry(ULONG initial_input)
{
    (VOID)initial_input;

    /* This thread has two jobs:
       1. Consume BMI160 INT1 anymotion semaphore — when the car moves
          horizontally (X/Y axes), reset the auto-sleep timer so the
          device stays awake while driving.
       2. Auto-sleep watchdog: if no motion AND no button for 60 s,
          enter STOP2 deep sleep.  BMI160 INT1 remains a wake source
          so the device wakes when the car moves again. */

    ULONG timeout_ticks = (ULONG)DEEP_SLEEP_TIMEOUT_SEC * TX_TIMER_TICKS_PER_SECOND;

    while (1)
    {
        /* Wait up to 1 s for a motion interrupt.
           If motion happens, we get TX_SUCCESS immediately. */
        UINT sem_status = tx_semaphore_get(&motion_sem, TX_TIMER_TICKS_PER_SECOND);

        /* Skip when already sleeping */
        if (system_sleeping)
            continue;

        if (sem_status == TX_SUCCESS)
        {
            /* ── BMI160 INT1 fired: horizontal motion detected ──────── */

            /* Read INT_STATUS_2 to confirm which axis triggered */
            uint8_t stat2 = BMI160_GetIntStatus2();
            BMI160_ClearInterrupt();

            /* Only count X/Y motion (horizontal plane on the car) */
            if (stat2 & (BMI160_ANYM_1ST_X_BIT | BMI160_ANYM_1ST_Y_BIT))
            {
                /* Car is moving — keep device awake */
                reset_activity();
            }

            /* Drain any extra semaphore counts from rapid INT1 edges */
            while (tx_semaphore_get(&motion_sem, TX_NO_WAIT) == TX_SUCCESS) {}
        }

        /* ── Auto-sleep watchdog check ───────────────────────────────── */
        if (!sleep_mode_enabled || battery_showing)
            continue;

        ULONG now     = tx_time_get();
        ULONG elapsed = now - last_activity_tick;

        if (elapsed >= timeout_ticks)
        {
            /* 60 s of no horizontal motion and no button → deep sleep.
               BMI160 in low-power mode still fires INT1 on motion,
               which will wake the MCU from STOP2. */
            ignore_wake_press = 1;
            System_EnterDeepSleep();
        }
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 * Battery icon helper (dummy — no ADC hardware)
 * ──────────────────────────────────────────────────────────────────────────*/

/**
  * @brief  Draw a dummy battery icon on the OLED for ~3 seconds, then clear.
  *         Displays a battery outline with a green fill bar (75 %).
  */
static void show_battery_icon(void)
{
    /* Tell oled_thread to stop touching the display */
    battery_showing = 1;

    /* Pause animation while showing battery */
    AnimPlayer_Stop(&anim_player);

    /* Give oled_thread time to see the flag and stop */
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 5);

    SSD1331_FillScreen(COLOR_BLACK);

    /* Battery outline: 40×20 at center of 96×64 display */
    uint8_t bx = 28, by = 22, bw = 40, bh = 20;

    /* Outline */
    SSD1331_DrawRect(bx, by, bw, bh, COLOR_WHITE);
    /* Terminal nub */
    SSD1331_FillRect(bx + bw, by + 6, 4, 8, COLOR_WHITE);

    /* Fill bar — dummy 75 % level, green */
    uint8_t fill_w = (uint8_t)((bw - 4) * 75 / 100);
    SSD1331_FillRect(bx + 2, by + 2, fill_w, bh - 4, COLOR_GREEN);

    /* Show icon: at least 1 second, or until button released — whichever is longer */
    ULONG t0 = tx_time_get();
    ButtonHandler_WaitRelease();
    ULONG elapsed = tx_time_get() - t0;
    if (elapsed < TX_TIMER_TICKS_PER_SECOND)
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND - elapsed);

    /* Clear and let animation resume naturally */
    SSD1331_FillScreen(COLOR_BLACK);

    /* Release oled_thread — it will resume animation */
    battery_showing = 0;
}

/* ──────────────────────────────────────────────────────────────────────────
 * Button thread — dispatches gestures to system actions
 * ──────────────────────────────────────────────────────────────────────────*/

/* Helper: ms → ThreadX ticks */
static inline ULONG ms_to_ticks_helper(uint32_t ms)
{
    return (ULONG)(((uint64_t)ms * TX_TIMER_TICKS_PER_SECOND + 999) / 1000);
}

static VOID button_thread_entry(ULONG initial_input)
{
    (VOID)initial_input;

    while (1)
    {
        ButtonEvent_t evt = ButtonHandler_WaitEvent(TX_WAIT_FOREVER);

        /* Any button gesture = user activity → reset auto-sleep timer */
        if (evt != BTN_EVT_NONE)
        {
            reset_activity();

            /* After auto-sleep wake, discard the button press that woke
               the MCU so it doesn't trigger another action. */
            if (ignore_wake_press)
            {
                ignore_wake_press = 0;
                continue;
            }
        }

        switch (evt)
        {
        /* ── Single press: toggle system ON / OFF ────────────────────── */
        case BTN_EVT_SINGLE:
            if (!system_sleeping)
            {
                /* Enter true STOP2 deep sleep (~3-5 µA).
                   This function BLOCKS until button wakes the MCU.
                   On return, everything is fully restored. */
                System_EnterDeepSleep();
            }
            /* If system_sleeping is set (shouldn't happen — we're in STOP2),
               the wake-up is handled inside System_EnterDeepSleep(). */
            break;

        /* ── Long press: show battery on OLED ────────────────────────── */
        case BTN_EVT_LONG:
            if (!system_sleeping)
            {
                show_battery_icon();
            }
            break;

        /* ── Double press: toggle gyro ON / OFF ──────────────────────── */
        case BTN_EVT_DOUBLE:
            if (!system_sleeping)
            {
                gyro_enabled = !gyro_enabled;

                /* Block oled_thread from touching the display */
                battery_showing = 1;
                AnimPlayer_Stop(&anim_player);
                tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 5);

                SSD1331_FillScreen(COLOR_BLACK);

                if (gyro_enabled)
                {
                    /* Start gyroscope in normal mode */
                    BMI160_ReadReg(BMI160_REG_ERR_REG);
                    BMI160_ReadReg(BMI160_REG_CMD);
                    uint8_t cmd_data[2] = { BMI160_REG_CMD & 0x7F, BMI160_CMD_GYR_NORMAL };
                    HAL_GPIO_WritePin(BMI160_CS_GPIO_Port, BMI160_CS_Pin, GPIO_PIN_RESET);
                    HAL_SPI_Transmit(&hspi2, cmd_data, 2, 100);
                    HAL_GPIO_WritePin(BMI160_CS_GPIO_Port, BMI160_CS_Pin, GPIO_PIN_SET);

                    /* Draw GYRO ON icon: green rotating arrows (circular shape) */
                    /* Outer circle outline using lines */
                    uint8_t cx = 48, cy = 24, r = 14;
                    SSD1331_DrawRect(cx - r, cy - r, r * 2, r * 2, COLOR_GREEN);
                    /* Inner cross = rotation symbol */
                    SSD1331_DrawLine(cx - 8, cy, cx + 8, cy, COLOR_GREEN);
                    SSD1331_DrawLine(cx, cy - 8, cx, cy + 8, COLOR_GREEN);
                    /* Arrow tips on the cross */
                    SSD1331_DrawLine(cx + 6, cy - 3, cx + 8, cy, COLOR_GREEN);
                    SSD1331_DrawLine(cx + 6, cy + 3, cx + 8, cy, COLOR_GREEN);
                    SSD1331_DrawLine(cx - 3, cy - 8, cx, cy - 8, COLOR_GREEN);
                    SSD1331_DrawLine(cx + 3, cy - 8, cx, cy - 8, COLOR_GREEN);
                    /* "ON" bar below */
                    SSD1331_FillRect(cx - 12, cy + 20, 24, 4, COLOR_GREEN);
                }
                else
                {
                    /* Suspend gyroscope */
                    uint8_t cmd_data[2] = { BMI160_REG_CMD & 0x7F, BMI160_CMD_GYR_SUSPEND };
                    HAL_GPIO_WritePin(BMI160_CS_GPIO_Port, BMI160_CS_Pin, GPIO_PIN_RESET);
                    HAL_SPI_Transmit(&hspi2, cmd_data, 2, 100);
                    HAL_GPIO_WritePin(BMI160_CS_GPIO_Port, BMI160_CS_Pin, GPIO_PIN_SET);

                    /* Draw GYRO OFF icon: red square with X */
                    uint8_t cx = 48, cy = 24, r = 14;
                    SSD1331_DrawRect(cx - r, cy - r, r * 2, r * 2, COLOR_RED);
                    /* Big X */
                    SSD1331_DrawLine(cx - 10, cy - 10, cx + 10, cy + 10, COLOR_RED);
                    SSD1331_DrawLine(cx - 10, cy + 10, cx + 10, cy - 10, COLOR_RED);
                    /* "OFF" bar below (red, shorter dashes) */
                    SSD1331_FillRect(cx - 12, cy + 20, 8, 4, COLOR_RED);
                    SSD1331_FillRect(cx + 4, cy + 20, 8, 4, COLOR_RED);
                }

                /* Hold for 3 seconds, then clear and resume animation */
                tx_thread_sleep(3 * TX_TIMER_TICKS_PER_SECOND);
                SSD1331_FillScreen(COLOR_BLACK);
                battery_showing = 0;
            }
            break;

        /* ── Triple press: toggle auto-sleep mode ────────────────────── */
        case BTN_EVT_TRIPLE:
            if (!system_sleeping)
            {
                sleep_mode_enabled = !sleep_mode_enabled;

                /* Block oled_thread from touching the display */
                battery_showing = 1;
                AnimPlayer_Stop(&anim_player);
                tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 5);

                SSD1331_FillScreen(COLOR_BLACK);

                /* Moon icon: crescent shape */
                uint8_t cx = 48, cy = 22;

                if (sleep_mode_enabled)
                {
                    /* SLEEP ON: blue crescent moon + "ZZZ" */
                    /* Moon body (filled) */
                    SSD1331_FillRect(cx - 10, cy - 12, 16, 24, COLOR_BLUE);
                    /* Cut-out to create crescent (black circle overlay) */
                    SSD1331_FillRect(cx - 2, cy - 10, 14, 20, COLOR_BLACK);
                    /* ZZZ */
                    SSD1331_DrawLine(cx + 8, cy - 10, cx + 18, cy - 10, COLOR_CYAN);
                    SSD1331_DrawLine(cx + 18, cy - 10, cx + 8, cy - 4, COLOR_CYAN);
                    SSD1331_DrawLine(cx + 8, cy - 4, cx + 18, cy - 4, COLOR_CYAN);
                    SSD1331_DrawLine(cx + 12, cy - 2, cx + 18, cy - 2, COLOR_CYAN);
                    SSD1331_DrawLine(cx + 18, cy - 2, cx + 12, cy + 2, COLOR_CYAN);
                    SSD1331_DrawLine(cx + 12, cy + 2, cx + 18, cy + 2, COLOR_CYAN);
                    /* "ON" bar below */
                    SSD1331_FillRect(cx - 12, cy + 20, 24, 4, COLOR_BLUE);
                }
                else
                {
                    /* SLEEP OFF: gray moon + red X over it */
                    SSD1331_FillRect(cx - 10, cy - 12, 16, 24, COLOR_GRAY);
                    SSD1331_FillRect(cx - 2, cy - 10, 14, 20, COLOR_BLACK);
                    /* Red X across the icon */
                    SSD1331_DrawLine(cx - 14, cy - 14, cx + 14, cy + 14, COLOR_RED);
                    SSD1331_DrawLine(cx - 14, cy + 14, cx + 14, cy - 14, COLOR_RED);
                    /* "OFF" dashed bar below */
                    SSD1331_FillRect(cx - 12, cy + 20, 8, 4, COLOR_RED);
                    SSD1331_FillRect(cx + 4, cy + 20, 8, 4, COLOR_RED);
                }

                /* Hold for 3 seconds, then clear and resume */
                tx_thread_sleep(3 * TX_TIMER_TICKS_PER_SECOND);
                SSD1331_FillScreen(COLOR_BLACK);
                battery_showing = 0;
            }
            break;

        /* ── Quadruple press: toggle mute ────────────────────────────── */
        case BTN_EVT_QUAD:
            if (!system_sleeping)
            {
                if (AudioPlayer_IsMuted())
                    AudioPlayer_Unmute();
                else
                    AudioPlayer_Mute();

                /* Block oled_thread from touching the display */
                battery_showing = 1;
                AnimPlayer_Stop(&anim_player);
                tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 5);

                SSD1331_FillScreen(COLOR_BLACK);

                /* Speaker icon center */
                uint8_t sx = 30, sy = 16;

                /* Speaker body: small rectangle */
                SSD1331_FillRect(sx, sy + 2, 8, 12, COLOR_WHITE);
                /* Speaker cone: triangle using fill */
                SSD1331_FillTriangle(sx + 8, sy + 2, sx + 8, sy + 14, sx + 18, sy - 4, COLOR_WHITE);
                SSD1331_FillTriangle(sx + 8, sy + 14, sx + 18, sy - 4, sx + 18, sy + 20, COLOR_WHITE);

                if (!AudioPlayer_IsMuted())
                {
                    /* UNMUTED: green sound waves */
                    /* Wave 1 (close) */
                    SSD1331_DrawLine(sx + 22, sy + 2, sx + 24, sy - 2, COLOR_GREEN);
                    SSD1331_DrawLine(sx + 24, sy - 2, sx + 24, sy + 18, COLOR_GREEN);
                    SSD1331_DrawLine(sx + 24, sy + 18, sx + 22, sy + 14, COLOR_GREEN);
                    /* Wave 2 (far) */
                    SSD1331_DrawLine(sx + 28, sy - 2, sx + 30, sy - 6, COLOR_GREEN);
                    SSD1331_DrawLine(sx + 30, sy - 6, sx + 30, sy + 22, COLOR_GREEN);
                    SSD1331_DrawLine(sx + 30, sy + 22, sx + 28, sy + 18, COLOR_GREEN);
                    /* "ON" bar below */
                    SSD1331_FillRect(38, sy + 28, 24, 4, COLOR_GREEN);
                }
                else
                {
                    /* MUTED: red X over speaker */
                    SSD1331_DrawLine(sx + 20, sy - 2, sx + 34, sy + 18, COLOR_RED);
                    SSD1331_DrawLine(sx + 20, sy + 18, sx + 34, sy - 2, COLOR_RED);
                    /* thicker X */
                    SSD1331_DrawLine(sx + 21, sy - 2, sx + 35, sy + 18, COLOR_RED);
                    SSD1331_DrawLine(sx + 21, sy + 18, sx + 35, sy - 2, COLOR_RED);
                    /* "OFF" dashed bar below */
                    SSD1331_FillRect(38, sy + 28, 8, 4, COLOR_RED);
                    SSD1331_FillRect(54, sy + 28, 8, 4, COLOR_RED);
                }

                /* Hold for 3 seconds, then clear and resume */
                tx_thread_sleep(3 * TX_TIMER_TICKS_PER_SECOND);
                SSD1331_FillScreen(COLOR_BLACK);
                battery_showing = 0;
            }
            break;

        default:
            break;
        }
    }
}

/**
  * @brief  EXTI GPIO callbacks — called from HAL IRQ handlers
  */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BMI160_INT1_Pin)
    {
        tx_semaphore_put(&motion_sem);
    }
    if (GPIO_Pin == BTN_USER_Pin)
    {
        ButtonHandler_IRQ_Notify();
    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BTN_USER_Pin)
    {
        ButtonHandler_IRQ_Notify();
    }
}
/* USER CODE END 1 */
