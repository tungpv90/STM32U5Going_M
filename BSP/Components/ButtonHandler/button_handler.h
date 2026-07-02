/**
  ******************************************************************************
  * @file    button_handler.h
  * @brief   Single-button multi-gesture detector for ThreadX.
  *
  *          Supported gestures:
  *            - Single press    → BTN_EVT_SINGLE
  *            - Long press      → BTN_EVT_LONG   (held ≥ 800 ms)
  *            - Double press    → BTN_EVT_DOUBLE
  *            - Triple press    → BTN_EVT_TRIPLE
  *            - Quadruple press → BTN_EVT_QUAD
  *
  *          Button hardware: active-low with internal pull-up (PA0 → GND).
  *          EXTI0 configured for rising + falling edges.
  *
  ******************************************************************************
  */

#ifndef __BUTTON_HANDLER_H__
#define __BUTTON_HANDLER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "tx_api.h"

/* --- Timing constants (milliseconds) -------------------------------------*/
#define BTN_DEBOUNCE_MS       50    /* Software debounce period              */
#define BTN_LONG_PRESS_MS     800   /* Hold ≥ this → long press             */
#define BTN_MULTI_WINDOW_MS   400   /* Max gap between consecutive presses   */

/* --- Gesture event enumeration -------------------------------------------*/
typedef enum {
    BTN_EVT_NONE   = 0,
    BTN_EVT_SINGLE,       /* 1 press  → toggle ON/OFF                      */
    BTN_EVT_LONG,         /* held ≥ 800 ms → show battery                  */
    BTN_EVT_DOUBLE,       /* 2 presses → toggle gyro                       */
    BTN_EVT_TRIPLE,       /* 3 presses → toggle sleep mode                 */
    BTN_EVT_QUAD          /* 4 presses → toggle mute                       */
} ButtonEvent_t;

/* --- Public API ----------------------------------------------------------*/

/**
  * @brief  Initialise the button handler (creates internal semaphore).
  *         Call once from App_ThreadX_Init() BEFORE starting the button thread.
  * @retval TX_SUCCESS on success
  */
UINT ButtonHandler_Init(void);

/**
  * @brief  Notify the handler of a GPIO edge (rising or falling).
  *         Call from EXTI ISR — safe to call from interrupt context.
  */
void ButtonHandler_IRQ_Notify(void);

/**
  * @brief  Block until a gesture is detected and return it.
  *         Runs the internal state machine; must be called from a thread.
  * @param  timeout_ticks  ThreadX tick timeout (TX_WAIT_FOREVER for infinite)
  * @retval Detected gesture, or BTN_EVT_NONE on timeout
  */
ButtonEvent_t ButtonHandler_WaitEvent(ULONG timeout_ticks);

/**
  * @brief  Block until button is released.  Call after BTN_EVT_LONG
  *         to implement “hold to show, release to hide” behavior.
  */
void ButtonHandler_WaitRelease(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUTTON_HANDLER_H__ */
