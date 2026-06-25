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
#include "halo75_v2_internal.h"
#include "hal_usb.h"
#include "usb_main.h"
#include "mcu_pwr.h"

/**
 * @brief  Wake up from light sleep — called by pre_process_record_kb
 *         on the first keypress for instant wakeup (no 50ms poll delay).
 */
void wakeup_handle(void) {
    if (!f_wakeup_prepare) return;

    f_wakeup_prepare = 0;
    no_act_time      = 0;

    exit_light_sleep();

    if (dev_info.link_mode == LINK_USB) {
#define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
        if ((USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED)) {
            usb_lld_wakeup_host(&USB_DRIVER);
        }
    }
}

/**
 * @brief  Sleep Handle — runs every 50ms from housekeeping_task_kb.
 *
 * Three independent toggles control sleep behaviour:
 *   f_dev_sleep_enable  — master switch (SLEEP_MODE keycode)
 *   f_usb_sleep_enable  — allow light sleep on USB suspend / inactivity
 *   f_deep_sleep_enable — allow deep sleep (STOP mode) on RF+battery
 *
 * sleep_timeout (1–60 min) replaces the fixed SLEEP_TIME_DELAY.
 */
void Sleep_Handle(void) {
    static uint32_t delay_step_timer     = 0;
    static uint8_t  usb_suspend_debounce = 0;
    static uint32_t rf_disconnect_time   = 0;

    /* 50ms interval */
    if (timer_elapsed32(delay_step_timer) < 50) return;
    delay_step_timer = timer_read32();

    /* Master sleep toggle off — nothing to do. */
    if (!f_dev_sleep_enable) return;

    uint32_t sleep_ticks = SLEEP_TIMEOUT_TO_TICKS(user_config.ee_sleep_timeout);

    /* sleep process */
    if (f_goto_sleep) {
        f_goto_sleep         = 0;
        usb_suspend_debounce = 0;
        rf_disconnect_time   = 0;
        rf_linking_time      = 0;

        if (dev_info.link_mode == LINK_USB) {
            /* USB: light sleep only if usb_sleep is enabled or host
             * actually suspended the bus. */
            if (f_usb_sleep_enable || USB_DRIVER.state == USB_SUSPENDED) {
                m_break_all_key();
                enter_light_sleep();
                f_wakeup_prepare = 1;
            }
        } else if ((dev_info.rf_charge & 0x01) != 0 || dev_info.rf_charge == 0x03) {
            /* RF + charging: light sleep (MCU must supervise charging). */
            m_break_all_key();
            enter_light_sleep();
            f_wakeup_prepare = 1;
        } else {
            /* RF + battery: deep sleep if enabled, else light sleep. */
            m_break_all_key();
            if (f_deep_sleep_enable) {
                enter_deep_sleep();
                exit_deep_sleep();
                no_act_time = 0;
                return;
            } else {
                enter_light_sleep();
                f_wakeup_prepare = 1;
            }
        }
    }

    /* sleep check */
    if (f_goto_sleep || f_wakeup_prepare) return;

    if (dev_info.link_mode == LINK_USB) {
        if (USB_DRIVER.state == USB_SUSPENDED) {
            usb_suspend_debounce++;
            if (usb_suspend_debounce >= 20) {
                f_goto_sleep = 1;
            }
        } else {
            usb_suspend_debounce = 0;
            if (f_usb_sleep_enable && no_act_time >= sleep_ticks) {
                f_goto_sleep = 1;
            }
        }
    } else if (dev_info.rf_state == RF_CONNECT) {
        rf_disconnect_time = 0;
        if (no_act_time >= sleep_ticks) {
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
