/**
  ******************************************************************************
  * @file    button_handler.c
  * @brief   Single-button multi-gesture detector for ThreadX.
  *
  *          State machine:
  *
  *          IDLE ──(falling edge)──→ PRESSED
  *          PRESSED ──(rising edge, held ≥ 800 ms)──→ emit LONG_PRESS → IDLE
  *          PRESSED ──(rising edge, held < 800 ms)──→ press_count++ → WAITING
  *          WAITING ──(falling edge within 400 ms)──→ PRESSED
  *          WAITING ──(400 ms timeout, no edge)──→ emit PRESS_N → IDLE
  *
  *          Button: active-low (pressed = GPIO LOW, released = GPIO HIGH).
  *          EXTI0 triggers on both edges; ISR posts a semaphore.
  *
  ******************************************************************************
  */

#include "button_handler.h"
#include "main.h"
#include "tx_api.h"

/* --- Internal state ------------------------------------------------------*/

static TX_SEMAPHORE btn_sem;   /* posted by ISR on every EXTI edge */

/* Helper: convert ms to ThreadX ticks (ceiling) */
static inline ULONG ms_to_ticks(uint32_t ms)
{
    return (ULONG)(((uint64_t)ms * TX_TIMER_TICKS_PER_SECOND + 999) / 1000);
}

/* Helper: read button level (1 = released / HIGH, 0 = pressed / LOW) */
static inline uint8_t btn_is_pressed(void)
{
    return (HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin) == GPIO_PIN_RESET) ? 1 : 0;
}

/* --- Public API ----------------------------------------------------------*/

UINT ButtonHandler_Init(void)
{
    return tx_semaphore_create(&btn_sem, "BtnSem", 0);
}

void ButtonHandler_IRQ_Notify(void)
{
    tx_semaphore_put(&btn_sem);
}

/**
  * @brief  Wait for and classify the next button gesture.
  *
  *         The function blocks on a semaphore that the EXTI ISR posts on
  *         every edge.  It then runs a small state machine that tracks
  *         press/release timing to distinguish single, long, double,
  *         triple, and quadruple presses.
  */
ButtonEvent_t ButtonHandler_WaitEvent(ULONG timeout_ticks)
{
    ULONG debounce_ticks = ms_to_ticks(BTN_DEBOUNCE_MS);
    ULONG long_ticks     = ms_to_ticks(BTN_LONG_PRESS_MS);
    ULONG window_ticks   = ms_to_ticks(BTN_MULTI_WINDOW_MS);

    /* Drain any stale edges */
    while (tx_semaphore_get(&btn_sem, TX_NO_WAIT) == TX_SUCCESS) {}

    /* ── STATE: IDLE — wait for first press (falling edge) ─────────────── */
    while (1)
    {
        if (tx_semaphore_get(&btn_sem, timeout_ticks) != TX_SUCCESS)
            return BTN_EVT_NONE;  /* timeout */

        /* Debounce */
        tx_thread_sleep(debounce_ticks);

        if (btn_is_pressed())
            break;  /* genuine press detected */

        /* Spurious edge — keep waiting */
    }

    /* ── STATE: PRESSED — record press timestamp ───────────────────────── */
    ULONG press_time = tx_time_get();
    uint8_t press_count = 0;

    while (1)
    {
        /* Wait for release (rising edge) or long-press timeout */
        UINT status = tx_semaphore_get(&btn_sem, long_ticks);

        tx_thread_sleep(debounce_ticks);

        if (btn_is_pressed())
        {
            /* Still pressed — check if long-press threshold crossed */
            ULONG now = tx_time_get();
            ULONG held = now - press_time;

            if (held >= long_ticks)
            {
                /* Return immediately — caller uses WaitRelease() */
                return BTN_EVT_LONG;
            }

            /* Not yet long — continue waiting */
            continue;
        }

        /* ── Button released ── */
        ULONG now = tx_time_get();
        ULONG held = now - press_time;

        if (status != TX_SUCCESS || held >= long_ticks)
        {
            /* Timeout while pressed → long press */
            return BTN_EVT_LONG;
        }

        /* Short press completed */
        press_count++;

        /* ── STATE: WAITING — wait for another press within window ───── */
        ULONG window_start = tx_time_get();
        uint8_t got_next_press = 0;

        while (1)
        {
            ULONG elapsed = tx_time_get() - window_start;
            if (elapsed >= window_ticks)
                break;  /* window expired */

            ULONG remaining = window_ticks - elapsed;
            if (tx_semaphore_get(&btn_sem, remaining) != TX_SUCCESS)
                break;  /* timeout */

            tx_thread_sleep(debounce_ticks);

            if (btn_is_pressed())
            {
                /* New press within window */
                press_time = tx_time_get();
                got_next_press = 1;
                break;
            }
            /* Spurious edge — keep waiting in window */
        }

        if (!got_next_press)
        {
            /* Window expired — classify by press_count */
            switch (press_count)
            {
                case 1:  return BTN_EVT_SINGLE;
                case 2:  return BTN_EVT_DOUBLE;
                case 3:  return BTN_EVT_TRIPLE;
                default: return BTN_EVT_QUAD;   /* 4+ treated as quad */
            }
        }

        /* Got another press → loop back to PRESSED state */
    }
}

void ButtonHandler_WaitRelease(void)
{
    ULONG debounce_ticks = ms_to_ticks(BTN_DEBOUNCE_MS);

    while (btn_is_pressed())
    {
        tx_semaphore_get(&btn_sem, ms_to_ticks(100));
        tx_thread_sleep(debounce_ticks);
    }

    /* Drain stale edges from the release */
    while (tx_semaphore_get(&btn_sem, TX_NO_WAIT) == TX_SUCCESS) {}
}
