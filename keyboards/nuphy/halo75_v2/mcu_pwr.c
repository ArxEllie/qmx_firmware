/*
Copyright 2023 NuPhy, Persama (@Persama) & jincao1

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "halo75_v2.h"
#include "halo75_v2_internal.h"
#include "mcu_pwr.h"
#include "hal_usb.h"
#include "usb_main.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Pin / register definitions                                         */
/* ------------------------------------------------------------------ */

/* Matrix pin tables — pulled from keyboard.json at build time. */
static const pin_t row_pins[MATRIX_ROWS] = MATRIX_ROW_PINS;
static const pin_t col_pins[MATRIX_COLS] = MATRIX_COL_PINS;

/* State tracking */
static bool     sleeping   = false;
static bool     rgb_led_on = true;
static uint32_t deep_sleep_enabled_irqs;
static uint32_t deep_sleep_systick_ctrl;

/* ------------------------------------------------------------------ */
/*  LED power control                                                  */
/* ------------------------------------------------------------------ */

/* On the Halo75 V2, the RGB IS31FL3733 drivers and side LEDs share the
 * same power rail: DC_BOOST_PIN (C2) and the SDB shutdown pins (C6, C7).
 * Setting SDB low + DC boost off cuts ~30 mA of LED current in sleep. */

static void rgb_power_off(void) {
    gpio_set_pin_output(DC_BOOST_PIN);
    gpio_write_pin_low(DC_BOOST_PIN);
    gpio_set_pin_input(RGB_DRIVER_SDB1);
    gpio_set_pin_input(RGB_DRIVER_SDB2);
}

static void rgb_power_on(void) {
    gpio_set_pin_output(DC_BOOST_PIN);
    gpio_write_pin_high(DC_BOOST_PIN);
    gpio_set_pin_output(RGB_DRIVER_SDB1);
    gpio_write_pin_high(RGB_DRIVER_SDB1);
    gpio_set_pin_output(RGB_DRIVER_SDB2);
    gpio_write_pin_high(RGB_DRIVER_SDB2);
}

void pwr_rgb_led_off(void) {
    if (!rgb_led_on) return;
    rgb_power_off();
    rgb_led_on = false;
}

void pwr_rgb_led_on(void) {
    if (sleeping || rgb_led_on) return;
    rgb_power_on();
    rgb_led_on = true;
}

bool is_rgb_led_on(void) {
    return rgb_led_on;
}

void led_pwr_sleep_handle(void) {
    pwr_rgb_led_off();
}

void led_pwr_wake_handle(void) {
    pwr_rgb_led_on();
    /* Push a fresh PWM buffer so the LEDs don't show garbage after
     * the driver was held in shutdown. */
    rgb_matrix_update_pwm_buffers();
}

/* ------------------------------------------------------------------ */
/*  EXTI configuration helpers (direct register access)                */
/* ------------------------------------------------------------------ */

/* Configure SYSCFG_EXTICR for a given pin source and port source.
 * STM32F0: EXTICR are 4-bit fields, 4 per 32-bit register. */
static void syscfg_exti_config(uint8_t port_source, uint8_t pin_source) {
    uint32_t shift = (pin_source & 0x03) * 4;
    uint32_t idx   = pin_source >> 2;
    SYSCFG->EXTICR[idx] &= ~(0x0F << shift);
    SYSCFG->EXTICR[idx] |= ((uint32_t)port_source << shift);
}

/* ------------------------------------------------------------------ */
/*  Deep sleep — STOP mode with EXTI wakeup                            */
/* ------------------------------------------------------------------ */

void prepare_deep_sleep(void) {
    /* These commands receive UART replies. Send them before STOP so the
     * normal housekeeping receive path can drain those replies instead of
     * letting USART wake the MCU immediately after WFI. */
    if (dev_info.rf_state == RF_CONNECT) {
        uart_send_cmd(CMD_SET_CONFIG, 0);
        uart_send_cmd(CMD_SLEEP, 0);
    } else {
        uart_send_cmd(CMD_SLEEP, 0);
    }
}

void enter_deep_sleep(void) {
    /* COL2ROW diode direction: diode anode on column, cathode on row.
     * Strategy: drive all columns HIGH, set rows as input pull-DOWN.
     * Idle: rows held LOW by pull-down.  Keypress: diode forward-biased
     * (col HIGH → row LOW), conducts and pulls row HIGH.
     * That gives us a rising edge that triggers EXTI. */
    for (int i = 0; i < ARRAY_SIZE(col_pins); i++) {
        gpio_set_pin_output(col_pins[i]);
        gpio_write_pin_high(col_pins[i]);
    }

    for (int i = 0; i < ARRAY_SIZE(row_pins); i++) {
        gpio_set_pin_input_low(row_pins[i]); /* pull-down */
    }

    /* Enable SYSCFG clock — required for EXTICR writes to take effect.
     * Without this, EXTI lines stay routed to their default port (PA)
     * and wakeup from pins on other ports never fires. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* Map each row pin to its EXTI line via SYSCFG. */
    syscfg_exti_config(EXTI_PORT_R0, EXTI_PIN_R0);
    syscfg_exti_config(EXTI_PORT_R1, EXTI_PIN_R1);
    syscfg_exti_config(EXTI_PORT_R2, EXTI_PIN_R2);
    syscfg_exti_config(EXTI_PORT_R3, EXTI_PIN_R3);
    syscfg_exti_config(EXTI_PORT_R4, EXTI_PIN_R4);
    syscfg_exti_config(EXTI_PORT_R5, EXTI_PIN_R5);

    /* Enable EXTI rising-edge interrupt only on the 6 row pin lines:
     *   A0=line0, A1=line1, A2=line2, A3=line3, C14=line14, C15=line15
     * Rows idle LOW (pull-down); keypress pulls row HIGH = rising edge.
     * Masking unused lines prevents spurious wakeups from floating pins. */
#define EXTI_ROW_MASK ((1U << EXTI_PIN_R0) | (1U << EXTI_PIN_R1) | (1U << EXTI_PIN_R2) | (1U << EXTI_PIN_R3) | (1U << EXTI_PIN_R4) | (1U << EXTI_PIN_R5))
    EXTI->IMR  = EXTI_ROW_MASK;
    EXTI->EMR  = 0x0000;
    EXTI->RTSR = EXTI_ROW_MASK; /* rising trigger on row lines */
    EXTI->FTSR = 0x0000;        /* no falling trigger */
    EXTI->PR   = 0xFFFF;        /* clear any pending edges */

    /* Enable NVIC IRQ channels for the EXTI groups our rows fall in:
     *   C14, C15 → EXTI4_15_IRQn
     *   A0, A1   → EXTI0_1_IRQn
     *   A2, A3   → EXTI2_3_IRQn */
    NVIC_EnableIRQ(EXTI4_15_IRQn);
    NVIC_SetPriority(EXTI4_15_IRQn, 0);
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_SetPriority(EXTI0_1_IRQn, 0);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
    NVIC_SetPriority(EXTI2_3_IRQn, 0);

    /* Power off LEDs before entering STOP. */
    led_pwr_sleep_handle();

    /* Drive dial switch pins low to save leakage current. */
    gpio_set_pin_output(DEV_MODE_PIN);
    gpio_write_pin_low(DEV_MODE_PIN);
    gpio_set_pin_output(SYS_MODE_PIN);
    gpio_write_pin_low(SYS_MODE_PIN);

    /* Put the nRF wakeup pin in a low-power state. */
    gpio_set_pin_input(NRF_WAKEUP_PIN);
    gpio_write_pin_low(NRF_WAKEUP_PIN);

    /* Clear RF report buffers so stale data isn't sent on wake. */
    memset(uart_bit_report_buf, 0, sizeof(uart_bit_report_buf));
    memset(bitkb_report_buf, 0, sizeof(bitkb_report_buf));
    memset(bytekb_report_buf, 0, sizeof(bytekb_report_buf));

    /* STOP must wake only for a matrix-row edge. USART replies, USB traffic,
     * I2C completion, and the ChibiOS system tick are not user activity and
     * otherwise make deep sleep return immediately.
     *
     * STM32F072 has fewer than 32 external IRQs, so one NVIC enable word
     * captures the complete pre-sleep interrupt state. The EXTI handlers are
     * re-enabled after masking, then the original mask is restored on wake. */
    _Static_assert(EXTI0_1_IRQn < 32 && EXTI2_3_IRQn < 32 && EXTI4_15_IRQn < 32, "deep-sleep EXTI IRQs must fit in NVIC word 0");
    deep_sleep_enabled_irqs = NVIC->ISER[0];
    deep_sleep_systick_ctrl = SysTick->CTRL;
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR     = SCB_ICSR_PENDSTCLR_Msk;
    NVIC->ICER[0] = UINT32_MAX;
    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
    NVIC_EnableIRQ(EXTI4_15_IRQn);

    /* Enter STOP mode: regulator in low-power, WFI for wakeup. */
    /* PDDS = 0 (STOP mode, not STANDBY), LPDS = 1 (low-power regulator). */
    PWR->CR |= PWR_CR_LPDS;
    PWR->CR &= ~PWR_CR_PDDS;

    /* Set SLEEPDEEP to enter STOP mode on WFI. */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    /* --- MCU is now asleep; execution resumes here on interrupt --- */

    /* Clear SLEEPDEEP immediately after wake. */
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    /* Disable all EXTI interrupts — the matrix scanner handles rows now. */
    EXTI->IMR  = 0x0000;
    EXTI->EMR  = 0x0000;
    EXTI->RTSR = 0x0000;
    EXTI->FTSR = 0x0000;
    EXTI->PR   = 0xFFFF;
    NVIC_DisableIRQ(EXTI4_15_IRQn);
    NVIC_DisableIRQ(EXTI0_1_IRQn);
    NVIC_DisableIRQ(EXTI2_3_IRQn);
}

void exit_deep_sleep(void) {
    /* After STOP mode, the MCU is running on HSI (8 MHz).  Reconfigure
     * the PLL and clock tree back to full speed. */
    stm32_clock_init();

    /* Restore the ChibiOS tick and every external interrupt that was enabled
     * before STOP. Do this only after the clock tree is stable so peripheral
     * handlers never run against the temporary HSI clock. */
    SysTick->CTRL = deep_sleep_systick_ctrl;
    NVIC->ISER[0] = deep_sleep_enabled_irqs;

    /* Restore dial switch pins to input for scanning. */
    gpio_set_pin_input_high(DEV_MODE_PIN);
    gpio_set_pin_input_high(SYS_MODE_PIN);

    /* Restore nRF wakeup pin. */
    gpio_set_pin_output(NRF_WAKEUP_PIN);
    gpio_write_pin_high(NRF_WAKEUP_PIN);

    /* Re-initialize the matrix pin configuration using the custom
     * matrix driver's init function.  This sets up port-level group
     * modes correctly for the fast palReadPort-based scanner. */
    extern void matrix_init_custom(void);
    matrix_init_custom();

    /* Power LEDs back on. */
    led_pwr_wake_handle();

    /* Tell the RF module to wake up. */
    uart_send_cmd_deferred(CMD_HAND, 1);

    /* Wake the USB host if we were suspended. */
    if (dev_info.link_mode == LINK_USB) {
        if (USB_DRIVER.state == USB_SUSPENDED) {
            usb_lld_wakeup_host(&USB_DRIVER);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Light sleep — LED power-off only, MCU keeps running                */
/* ------------------------------------------------------------------ */

void enter_light_sleep(void) {
    led_pwr_sleep_handle();
    sleeping = true;
}

void exit_light_sleep(void) {
    sleeping = false;
    led_pwr_wake_handle();

    /* Tell the RF module to wake up. */
    uart_send_cmd_deferred(CMD_HAND, 1);

    if (dev_info.link_mode == LINK_USB) {
        if (USB_DRIVER.state == USB_SUSPENDED) {
            usb_lld_wakeup_host(&USB_DRIVER);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  EXTI ISR stubs — just clear pending flags                          */
/* ------------------------------------------------------------------ */

/* STM32F0 EXTI IRQ handlers.  These fire on keypress during deep sleep
 * and serve only to wake the MCU from WFI.  The actual key is processed
 * by the normal matrix scan after exit_deep_sleep() restores pin config.
 * We must clear the pending flag so the ISR doesn't re-fire immediately. */

/* STM32F0 ChibiOS port doesn't define these EXTI handler macros
 * (unlike STM32L0/G0 which share the same Cortex-M0 vector layout).
 * Vector offsets for STM32F072:
 *   Vector54 = IRQ 5  (EXTI0_1)
 *   Vector58 = IRQ 6  (EXTI2_3)
 *   Vector5C = IRQ 7  (EXTI4_15) */
#define STM32_EXTI0_1_HANDLER Vector54
#define STM32_EXTI2_3_HANDLER Vector58
#define STM32_EXTI4_15_HANDLER Vector5C

OSAL_IRQ_HANDLER(STM32_EXTI0_1_HANDLER) {
    EXTI->PR = 0xFFFF;
}

OSAL_IRQ_HANDLER(STM32_EXTI2_3_HANDLER) {
    EXTI->PR = 0xFFFF;
}

OSAL_IRQ_HANDLER(STM32_EXTI4_15_HANDLER) {
    EXTI->PR = 0xFFFF;
}
