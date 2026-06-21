/*
Copyright 2023 @ Nuphy <https://nuphy.com/>

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
#include "hal_usb.h"
#include "usb_main.h"
#include "mcu_pwr.h"

extern user_config_t   user_config;
extern DEV_INFO_STRUCT dev_info;
extern uint16_t        rf_linking_time;
extern uint16_t        no_act_time;

extern bool f_wakeup_prepare;
extern bool f_goto_sleep;

uint8_t uart_send_cmd_deferred(uint8_t cmd, uint8_t delayms);

/* Break all keys before sleeping so the host doesn't see stuck keys. */
extern void m_break_all_key(void);

/**
 * @brief  Sleep Handle.
 */
void Sleep_Handle(void) {
    static uint32_t delay_step_timer     = 0;
    static uint8_t  usb_suspend_debounce = 0;
    static uint32_t rf_disconnect_time   = 0;
    static uint8_t  usb_wakeup_retry     = 0;
    static bool     usb_wakeup_pending   = false;

    /* 50ms interval */
    if (timer_elapsed32(delay_step_timer) < 50) return;
    delay_step_timer = timer_read32();

    if (usb_wakeup_pending) {
        if (USB_DRIVER.state == USB_SUSPENDED && usb_wakeup_retry) {
            usbWakeupHost(&USB_DRIVER);
            restart_usb_driver(&USB_DRIVER);
            usb_wakeup_retry--;
            return;
        }

        m_break_all_key();
        usb_wakeup_pending = false;
    }

    // sleep process
    if (f_goto_sleep) {
        f_goto_sleep = 0;
        usb_suspend_debounce = 0;
        rf_disconnect_time   = 0;
        rf_linking_time      = 0;

        if (!f_dev_sleep_enable) {
            /* Sleep disabled by user — just flag wakeup prep. */
            f_wakeup_prepare = 1;
        } else if (dev_info.link_mode == LINK_USB) {
            /* USB connected: light sleep only (MCU must stay awake for
             * USB suspend/resume signalling). */
            m_break_all_key();
            enter_light_sleep();
            f_wakeup_prepare = 1;
        } else if ((dev_info.rf_charge & 0x01) != 0 || dev_info.rf_charge == 0x03) {
            /* RF + charging: light sleep (can't deep sleep while
             * charging circuit needs MCU supervision). */
            m_break_all_key();
            enter_light_sleep();
            f_wakeup_prepare = 1;
        } else {
            /* RF + battery: deep sleep for maximum power savings.
             * enter_deep_sleep() blocks until a keypress wakes the MCU.
             * exit_deep_sleep() restores clocks, pins, and LEDs. */
            m_break_all_key();
            enter_deep_sleep();
            exit_deep_sleep();
            no_act_time = 0; /* prevent immediate re-sleep on wake */
            /* Don't set f_wakeup_prepare — we're already awake. */
        }
    }

    // wakeup check (light sleep only)
    if (f_wakeup_prepare && (no_act_time < 10)) {
        f_wakeup_prepare = 0;

        exit_light_sleep();

        if (dev_info.link_mode == LINK_USB) {
#define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
            if ((USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED)) {
                usb_lld_wakeup_host(&USB_DRIVER);
                usb_wakeup_retry   = 10;
                usb_wakeup_pending = true;
            }
        }
    }

    // sleep check
    if (f_goto_sleep || f_wakeup_prepare) return;
    if (dev_info.link_mode == LINK_USB) {
        if (USB_DRIVER.state == USB_SUSPENDED) {
            usb_suspend_debounce++;
            if (usb_suspend_debounce >= 20) {
                f_goto_sleep = 1;
            }
        } else {
            usb_suspend_debounce = 0;
        }
    } else if (dev_info.rf_state == RF_CONNECT) {
        rf_disconnect_time = 0;
        if (no_act_time >= SLEEP_TIME_DELAY) {
            f_goto_sleep = 1;
        }
    } else if (rf_linking_time >= LINK_TIMEOUT) {
        rf_linking_time = 0;
        f_goto_sleep    = 1;
    } else if (dev_info.rf_state == RF_DISCONNECT) {
        rf_disconnect_time++;
        if (rf_disconnect_time > 5 * 20) {
            rf_disconnect_time = 0;
            f_goto_sleep       = 1;
        }
    }
}
