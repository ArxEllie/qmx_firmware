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
#include "socd.h"
#include "usb_main.h"
#include "rf_driver.h"
#include "i2c_master.h"
#include "mcu_pwr.h"
#ifdef CONSOLE_ENABLE
#    include "debug.h"
#    include "matrix.h"
#endif

#ifdef CONSOLE_ENABLE
#    if (DIODE_DIRECTION == COL2ROW)
#        define DIODE_DIRECTION_STR "COL2ROW"
#    else
#        define DIODE_DIRECTION_STR "ROW2COL"
#    endif

/* Per-column row-mask logger for ghosting investigation.
 * Runs inside housekeeping_task_kb every loop but short-circuits
 * when no matrix row changed — O(MATRIX_ROWS) compare, zero allocation. */
static void debug_matrix_scan(void) {
    if (!debug_config.matrix) return;

    static matrix_row_t prev[MATRIX_ROWS] = {0};
    matrix_row_t        curr[MATRIX_ROWS];
    bool                changed = false;

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        curr[r] = matrix_get_row(r);
        if (curr[r] != prev[r]) changed = true;
    }
    if (!changed) return;

    extern matrix_row_t raw_matrix[MATRIX_ROWS];
    dprintf("RAW ");
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        dprintf("%08lX ", (uint32_t)raw_matrix[r]);
    }
    dprintf("\n");
    dprintf("DBN ");
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        dprintf("%08lX ", (uint32_t)curr[r]);
    }
    dprintf("\n");

    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        uint8_t mask = 0, prev_mask = 0, count = 0;
        char    rows_str[MATRIX_ROWS + 1];
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            if (curr[r] & (matrix_row_t)(1 << c)) {
                mask |= (1 << r);
                rows_str[count++] = '0' + r;
            }
            if (prev[r] & (matrix_row_t)(1 << c)) prev_mask |= (1 << r);
        }
        rows_str[count] = '\0';
        if (mask != prev_mask) {
            dprintf("C%d mask=%02X rows[0..%d]=%s\n", c, mask, count ? count - 1 : 0, rows_str);
        }
    }

    memcpy(prev, curr, sizeof(prev));
}
#endif /* CONSOLE_ENABLE */

user_config_t   user_config;
DEV_INFO_STRUCT dev_info = {
    .rf_battery = 100,
    .link_mode  = LINK_USB,
    .rf_state   = RF_IDLE,
};

uint16_t       rf_linking_time       = 0;
uint16_t       rf_link_show_time     = 0;
uint8_t        rf_blink_cnt          = 0;
uint32_t       no_act_time           = 0;
host_driver_t *m_host_driver         = 0;
uint16_t       dev_reset_press_delay = 0;
uint16_t       rf_sw_press_delay     = 0;
uint8_t        rf_sw_temp            = 0;
uint8_t        host_mode;

keyboard_flags_t kbd_flags = {
    .chg_show = true,
};

/* EEPROM write batching: mark dirty on settings changes, flush after
 * 500ms of no further changes to avoid rapid repeated flash writes. */
static bool     user_config_dirty  = false;
static uint32_t dirty_settle_timer = 0;

void user_config_mark_dirty(void) {
    user_config_dirty  = true;
    dirty_settle_timer = timer_read32();
}

void user_config_flush_if_dirty(void) {
    if (user_config_dirty && timer_elapsed32(dirty_settle_timer) >= EEPROM_FLUSH_DELAY_MS) {
        eeconfig_update_user_datablock(&user_config, 0, sizeof(user_config_t));
        user_config_dirty = false;
    }
}

/* Apply NKRO override after OS switch logic sets keymap_config.nkro.
 * In Auto mode, the OS switch value is kept. In On/Off, it's overridden. */
static void apply_nkro_override(void) {
    uint8_t mode = get_nkro_mode();
    if (mode == NKRO_ON) {
        if (!keymap_config.nkro) {
            keymap_config.nkro = 1;
            m_break_all_key();
        }
    } else if (mode == NKRO_OFF) {
        if (keymap_config.nkro) {
            keymap_config.nkro = 0;
            m_break_all_key();
        }
    }
}

/* Non-blocking device reset state machine.
 * Replaces the blocking wait_ms/uart_send_cmd chain that previously
 * froze the main loop for ~1.7 s during a factory reset. */
typedef enum {
    RESET_IDLE = 0,
    RESET_SET_LINK,    /* queue CMD_SET_LINK via deferred UART */
    RESET_WAIT,        /* 500 ms cool-down before clear */
    RESET_CLR_DEVICE,  /* queue CMD_CLR_DEVICE via deferred UART */
    RESET_EECONFIG,    /* eeconfig_init + start blink */
    RESET_BLINK,       /* 3× white/off, 200 ms each, timer-driven */
    RESET_INIT,        /* restore defaults, re-enable RGB, done */
} reset_state_t;

static reset_state_t dev_reset_state    = RESET_IDLE;
static uint32_t      dev_reset_timer    = 0;
static uint8_t       dev_reset_blink    = 0;
static bool          dev_reset_blink_on = false;

static void rgb_driver_gpio_init(void) {
    // RGB Matrix initializes before keyboard_post_init_kb(), so only the
    // LED power rail and IS31FL3733 shutdown pins are released here.
    gpio_set_pin_output(DC_BOOST_PIN);
    gpio_write_pin_high(DC_BOOST_PIN);

    gpio_set_pin_output(RGB_DRIVER_SDB1);
    gpio_write_pin_high(RGB_DRIVER_SDB1);
    gpio_set_pin_output(RGB_DRIVER_SDB2);
    gpio_write_pin_high(RGB_DRIVER_SDB2);
}

/**
 * @brief  gpio initial.
 */
void m_gpio_init(void) {
    gpio_set_pin_output(DC_BOOST_PIN);
    gpio_write_pin_high(DC_BOOST_PIN);

    // Initializes the RGB Driver SDB pin
    gpio_set_pin_output(RGB_DRIVER_SDB1);
    gpio_write_pin_high(RGB_DRIVER_SDB1);
    gpio_set_pin_output(RGB_DRIVER_SDB2);
    gpio_write_pin_high(RGB_DRIVER_SDB2);

    // RF wake up pin configuration
    gpio_set_pin_output(NRF_WAKEUP_PIN);
    gpio_write_pin_high(NRF_WAKEUP_PIN);

    // RFboot Control pin
    gpio_set_pin_input_high(NRF_BOOT_PIN);

    // RF reset pin configuration
    gpio_set_pin_output(NRF_RESET_PIN);
    gpio_write_pin_low(NRF_RESET_PIN);
    wait_ms(NRF_RESET_DELAY_MS);
    gpio_write_pin_high(NRF_RESET_PIN);

    // Switch detection pin
    gpio_set_pin_input_high(DEV_MODE_PIN);
    gpio_set_pin_input_high(SYS_MODE_PIN);
}

/**
 * @brief  long press key process.
 */
void long_press_key(void) {
    static uint32_t long_press_timer = 0;
    static uint8_t  new_adv_retry    = 0;

    if (timer_elapsed32(long_press_timer) < LONG_PRESS_POLL_MS) return;
    long_press_timer = timer_read32();

    if (new_adv_retry) {
        if (kbd_flags.rf_new_adv_ok) {
            new_adv_retry = 0;
        } else {
            uart_send_cmd_deferred(CMD_NEW_ADV, 1);
            new_adv_retry--;
        }
    }

    if (kbd_flags.rf_sw_press) {
        rf_sw_press_delay++;
        if (rf_sw_press_delay >= RF_LONG_PRESS_DELAY) {
            kbd_flags.rf_sw_press   = 0;
            dev_info.link_mode   = rf_sw_temp;
            dev_info.rf_channel  = rf_sw_temp;
            dev_info.ble_channel = rf_sw_temp;
            kbd_flags.rf_new_adv_ok  = 0;
            new_adv_retry        = 5;
        }
    } else {
        rf_sw_press_delay = 0;
    }

    if (kbd_flags.dev_reset_press && dev_reset_state == RESET_IDLE) {
        dev_reset_press_delay++;
        if (dev_reset_press_delay >= DEV_RESET_PRESS_DELAY) {
            kbd_flags.dev_reset_press = 0;

            /* Set link-mode defaults synchronously — just variable writes. */
            if (dev_info.link_mode != LINK_USB) {
                if (dev_info.link_mode != LINK_RF_24) {
                    dev_info.link_mode   = LINK_BT_1;
                    dev_info.ble_channel = LINK_BT_1;
                    dev_info.rf_channel  = LINK_BT_1;
                }
            } else {
                dev_info.ble_channel = LINK_BT_1;
            }

            /* Hand off to the non-blocking reset state machine. */
            dev_reset_state = RESET_SET_LINK;
        }
    } else {
        dev_reset_press_delay = 0;
    }
}

/**
 * @brief  Release all keys, clear keyboard report.
 */
void m_break_all_key(void) {
    uint8_t report_buf[16];

    clear_weak_mods();
    clear_mods();
    clear_keyboard();

    /* Send empty report on the *current* interface only.
     * Do NOT toggle keymap_config.nkro — switching the HID interface
     * mid-keystroke makes macOS drop held modifiers (Cmd+V garbled output). */
    memset(keyboard_report, 0, sizeof(report_keyboard_t));
    memset(nkro_report, 0, sizeof(report_nkro_t));
    if (keymap_config.nkro) {
        host_nkro_send(nkro_report);
    } else {
        host_keyboard_send(keyboard_report);
    }

    if (dev_info.link_mode != LINK_USB) {
        memset(report_buf, 0, 16);
        uart_send_report(CMD_RPT_BIT_KB, report_buf, 16);
        uart_send_report(CMD_RPT_BYTE_KB, report_buf, 8);
    }

    memset(uart_bit_report_buf, 0, sizeof(uart_bit_report_buf));
    memset(bitkb_report_buf, 0, sizeof(bitkb_report_buf));
    memset(bytekb_report_buf, 0, sizeof(bytekb_report_buf));

    socd_reset();
}

/**
 * @brief  switch device link mode.
 * @param mode : link mode
 */
static void switch_dev_link(uint8_t mode) {
    if (mode > LINK_USB) return;
    m_break_all_key();

    dev_info.link_mode = mode;
    dev_info.rf_state  = RF_IDLE;
    kbd_flags.send_channel  = 1;

    if (mode == LINK_USB) {
        host_mode = HOST_USB_TYPE;
        host_set_driver(m_host_driver);
        rf_link_show_time = 0;
    } else {
        host_mode = HOST_RF_TYPE;
        host_set_driver(&rf_host_driver);
    }
}

/**
 * @brief  scan dial switch.
 */
void dial_sw_scan(void) {
    uint8_t         dial_scan       = 0;
    static uint8_t  dial_save       = 0xf0;
    static uint8_t  debounce        = 0;
    static uint32_t dial_scan_timer = 0;
    static bool     flag_power_on   = 1;
    static uint8_t  dial_change_cnt = 0;

    if (!flag_power_on) {
        if (timer_elapsed32(dial_scan_timer) < DIAL_SCAN_INTERVAL_MS) return;
    }
    dial_scan_timer = timer_read32();

    gpio_set_pin_input_high(DEV_MODE_PIN);
    gpio_set_pin_input_high(SYS_MODE_PIN);

    if (gpio_read_pin(DEV_MODE_PIN)) dial_scan |= 0X01;
    if (gpio_read_pin(SYS_MODE_PIN)) dial_scan |= 0X02;

    if (dial_save != dial_scan) {
        if (++dial_change_cnt < DIAL_CHANGE_CONFIRM) return;
        dial_change_cnt = 0;
        m_break_all_key();
        dial_save         = dial_scan;
        no_act_time       = 0;
        rf_linking_time   = 0;
        debounce          = DIAL_DEBOUNCE_COUNT;
        kbd_flags.dial_sw_init_ok = 0;
        return;
    } else {
        dial_change_cnt = 0;
        if (debounce) {
            debounce--;
            return;
        }
    }

    if (dial_scan & 0x01) {
        if (dev_info.link_mode != LINK_USB) {
            switch_dev_link(LINK_USB);
        }
    } else {
        if (dev_info.link_mode != dev_info.rf_channel) {
            switch_dev_link(dev_info.rf_channel);
        }
    }

    if (dial_scan & 0x02) {
        if (dev_info.sys_sw_state != SYS_SW_WIN) {
            kbd_flags.sys_show = 1;
            default_layer_set(1 << 2);
            dev_info.sys_sw_state = SYS_SW_WIN;
            keymap_config.no_gui  = kbd_flags.win_lock;
            m_break_all_key();
        }
        keymap_config.nkro = 1;
    } else {
        if (dev_info.sys_sw_state != SYS_SW_MAC) {
            kbd_flags.sys_show = 1;
            default_layer_set(1 << 0);
            dev_info.sys_sw_state = SYS_SW_MAC;
            kbd_flags.win_lock      = keymap_config.no_gui;
            m_break_all_key();
        }
        keymap_config.nkro   = 0;
        keymap_config.no_gui = 0;
    }

    if (kbd_flags.dial_sw_init_ok == 0) {
        kbd_flags.dial_sw_init_ok = 1;
        flag_power_on     = 0;

        if (dev_info.link_mode != LINK_USB) {
            host_set_driver(&rf_host_driver);
        }
    }

    apply_nkro_override();
}

/**
 * @brief  power on scan dial switch.
 */
void m_power_on_dial_sw_scan(void) {
    uint8_t dial_scan_dev  = 0;
    uint8_t dial_scan_sys  = 0;
    uint8_t dial_check_dev = 0;
    uint8_t dial_check_sys = 0;
    uint8_t debounce       = 0;

    kbd_flags.win_lock = 0;

    gpio_set_pin_input_high(DEV_MODE_PIN);
    gpio_set_pin_input_high(SYS_MODE_PIN);

    for (debounce = 0; debounce < DIAL_POWER_ON_DEBOUNCE; debounce++) {
        dial_scan_dev = 0;
        dial_scan_sys = 0;
        if (gpio_read_pin(DEV_MODE_PIN))
            dial_scan_dev = 0x01;
        else
            dial_scan_dev = 0;
        if (gpio_read_pin(SYS_MODE_PIN))
            dial_scan_sys = 0x01;
        else
            dial_scan_sys = 0;
        if ((dial_scan_dev != dial_check_dev) || (dial_scan_sys != dial_check_sys)) {
            dial_check_dev = dial_scan_dev;
            dial_check_sys = dial_scan_sys;
            debounce       = 0;
        }
        wait_ms(1);
    }
    if (dial_scan_dev) {
        if (dev_info.link_mode != LINK_USB) {
            switch_dev_link(LINK_USB);
        }
    } else {
        if (dev_info.link_mode != dev_info.rf_channel) {
            switch_dev_link(dev_info.rf_channel);
        }
    }
    // WIN/MAC
    if (dial_scan_sys) {
        if (dev_info.sys_sw_state != SYS_SW_WIN) {
            default_layer_set(1 << 2); // WIN
            dev_info.sys_sw_state = SYS_SW_WIN;
            keymap_config.nkro    = 1;
            m_break_all_key();
        }
    } else {
        if (dev_info.sys_sw_state != SYS_SW_MAC) {
            default_layer_set(1 << 0); // MAC
            dev_info.sys_sw_state = SYS_SW_MAC;
            keymap_config.nkro    = 0;
            kbd_flags.win_lock      = keymap_config.no_gui;
            keymap_config.no_gui  = 0;
            m_break_all_key();
        }
    }

    apply_nkro_override();
}

static uint16_t macro_tap_keycode = KC_NO;
static uint32_t macro_tap_timer   = 0;

static void macro_tap_release(void) {
    if (macro_tap_keycode == KC_NO) {
        return;
    }

    unregister_code16(macro_tap_keycode);
    macro_tap_keycode = KC_NO;
}

static void macro_tap_deferred(uint16_t keycode) {
    macro_tap_release();
    register_code16(keycode);
    macro_tap_keycode = keycode;
    macro_tap_timer   = timer_read32();
}

static void macro_tap_task(void) {
    if (macro_tap_keycode != KC_NO && timer_elapsed32(macro_tap_timer) >= MACRO_TAP_HOLD_MS) {
        macro_tap_release();
    }
}

/**
 * @brief  qmk pre-process record — runs before all other record handlers.
 *         Used for instant wakeup from light sleep on first keypress,
 *         avoiding the 50ms Sleep_Handle poll delay.
 */
bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        wakeup_handle();
    }
    return true;
}

/* Handle wireless link switching (RF + BT1-3).
 * Short press: switch to the target channel.
 * Long press: enter pairing mode (handled by long_press_key in timer_pro). */
static bool handle_wireless_link(uint8_t link_target, keyrecord_t *record) {
    if (record->event.pressed) {
        if (dev_info.link_mode != LINK_USB) {
            rf_sw_temp    = link_target;
            kbd_flags.rf_sw_press = 1;
            m_break_all_key();
        }
    } else if (kbd_flags.rf_sw_press) {
        kbd_flags.rf_sw_press = 0;
        if (rf_sw_press_delay < RF_LONG_PRESS_DELAY) {
            dev_info.link_mode   = rf_sw_temp;
            dev_info.rf_channel  = rf_sw_temp;
            dev_info.ble_channel = rf_sw_temp;
            uart_send_cmd_deferred(CMD_SET_LINK, 20);
        }
    }
    return false;
}

/**
 * @brief  qmk process record
 */
bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!process_record_user(keycode, record)) {
        return false;
    }

#ifdef CONSOLE_ENABLE
    if (debug_config.matrix) {
        dprintf("EV k=%04X r=%d c=%d %s layer=%d mods=%02X weak=%02X oneshot=%02X row=%08lX\n",
                keycode,
                record->event.key.row, record->event.key.col,
                record->event.pressed ? "down" : "up",
                get_highest_layer(layer_state),
                get_mods(), get_weak_mods(), get_oneshot_mods(),
                (uint32_t)matrix_get_row(record->event.key.row));
    }
#endif

    no_act_time = 0;

    /* SOCD arrow-key interception — must run before the keycode switch
     * so SOCD can own register/unregister for arrow keys when active. */
    if (!socd_process_record(keycode, record)) {
        return false;
    }

#ifdef RGB_DEBUG
    /* RGB debug harness: repurpose FN-layer RGB matrix keycodes. */
    extern void rgb_debug_cycle_program(int8_t dir);
    extern void rgb_debug_step_value(int8_t dir);
    switch (keycode) {
        case RM_NEXT:
            if (record->event.pressed) rgb_debug_cycle_program(-1);
            return false;
        case RM_HUEU:
            if (record->event.pressed) rgb_debug_cycle_program(1);
            return false;
        case RM_SPDD:
            if (record->event.pressed) rgb_debug_step_value(-1);
            return false;
        case RM_SPDU:
            if (record->event.pressed) rgb_debug_step_value(1);
            return false;
        default:
            break;
    }
#endif

    switch (keycode) {
        /* ── RF / Link switching ─────────────────────────────────── */
        case RF_DFU:
            if (record->event.pressed) {
                if (dev_info.link_mode != LINK_USB) return false;
                uart_send_cmd_deferred(CMD_RF_DFU, 20);
            }
            return false;

        case LNK_USB:
            if (record->event.pressed) {
                m_break_all_key();
            } else {
                dev_info.link_mode = LINK_USB;
                uart_send_cmd_deferred(CMD_SET_LINK, 10);
            }
            return false;

        case LNK_RF:    return handle_wireless_link(LINK_RF_24, record);
        case LNK_BLE1:  return handle_wireless_link(LINK_BT_1, record);
        case LNK_BLE2:  return handle_wireless_link(LINK_BT_2, record);
        case LNK_BLE3:  return handle_wireless_link(LINK_BT_3, record);

        /* ── Mac media / system keys ─────────────────────────────── */
        case MAC_TASK:
            if (record->event.pressed) {
                host_consumer_send(0x029F);
            } else {
                host_consumer_send(0);
            }
            return false;

        case MAC_SEARCH:
            if (record->event.pressed) {
                macro_tap_deferred(LGUI(KC_SPACE));
            }
            return false;

        case MAC_VOICE:
            if (record->event.pressed) {
                host_consumer_send(0xcf);
            } else {
                host_consumer_send(0);
            }
            return false;

        case MAC_CONSOLE:
            if (record->event.pressed) {
                host_consumer_send(0x02A0);
            } else {
                host_consumer_send(0);
            }
            return false;

        case MAC_DND:
            if (record->event.pressed) {
                host_system_send(0x9b);
            } else {
                host_system_send(0);
            }
            return false;

        case MAC_PRT:
            if (record->event.pressed) {
                macro_tap_deferred(LGUI(LSFT(KC_3)));
            }
            return false;

        case MAC_PRTA:
            if (record->event.pressed) {
                // win
                if (keymap_config.nkro) {
                    macro_tap_deferred(LGUI(LSFT(KC_S)));
                }
                // mac
                else {
                    macro_tap_deferred(LGUI(LSFT(KC_4)));
                }
            }
            return false;

        /* ── Side LED controls ───────────────────────────────────── */
        case SIDE_VAI:
            if (record->event.pressed) {
                if (low_bat_flag && (side_light == 1)) return false;
                light_level_control(1);
                if (rgb_matrix_get_mode() == RGB_MATRIX_CUSTOM_lesbian_pride) {
                    rgb_matrix_increase_val();
                }
            }
            return false;

        case SIDE_VAD:
            if (record->event.pressed) {
                light_level_control(0);
                if (rgb_matrix_get_mode() == RGB_MATRIX_CUSTOM_lesbian_pride) {
                    rgb_matrix_decrease_val();
                }
            }
            return false;

        case SIDE_MOD_A:
            if (record->event.pressed) {
                side_mode_a_control(1);
            }
            return false;

        case SIDE_MOD_B:
            if (record->event.pressed) {
                side_mode_b_control(1);
            }
            return false;

        case SIDE_HUI:
            if (record->event.pressed) {
                side_colour_control(1);
            }
            return false;

        case SIDE_SPI:
            if (record->event.pressed) {
                light_speed_control(1);
            }
            return false;

        case SIDE_SPD:
            if (record->event.pressed) {
                light_speed_control(0);
            }
            return false;

        /* ── Device reset ────────────────────────────────────────── */
        case DEV_RESET:
            if (record->event.pressed) {
                kbd_flags.dev_reset_press = 1;
                m_break_all_key();
            } else {
                kbd_flags.dev_reset_press = 0;
            }
            return false;

        /* ── Settings: sleep, debounce, NKRO ──────────────────────── */
        case SLEEP_MODE:
            if (record->event.pressed) {
                if (f_dev_sleep_enable)
                    set_f_dev_sleep_enable(false);
                else
                    set_f_dev_sleep_enable(true);
                kbd_flags.sleep_show = 1;
                user_config_mark_dirty();
            }
            return false;

        case BAT_SHOW:
            if (record->event.pressed) {
                kbd_flags.bat_hold = !kbd_flags.bat_hold;
            }
            return false;

        case DEBOUNCE_PRESS_INC:
            if (record->event.pressed && user_config.ee_debounce_press_ms < 99) {
                user_config.ee_debounce_press_ms += DEBOUNCE_STEP;
                user_config_mark_dirty();
            }
            return false;
        case DEBOUNCE_PRESS_DEC:
            if (record->event.pressed && user_config.ee_debounce_press_ms > 1) {
                user_config.ee_debounce_press_ms -= DEBOUNCE_STEP;
                user_config_mark_dirty();
            }
            return false;
        case DEBOUNCE_RELEASE_INC:
            if (record->event.pressed && user_config.ee_debounce_release_ms < 99) {
                user_config.ee_debounce_release_ms += DEBOUNCE_STEP;
                user_config_mark_dirty();
            }
            return false;
        case DEBOUNCE_RELEASE_DEC:
            if (record->event.pressed && user_config.ee_debounce_release_ms > 1) {
                user_config.ee_debounce_release_ms -= DEBOUNCE_STEP;
                user_config_mark_dirty();
            }
            return false;

        case SLEEP_TIMEOUT_INC:
            if (record->event.pressed && user_config.ee_sleep_timeout < SLEEP_TIMEOUT_MAX) {
                user_config.ee_sleep_timeout += SLEEP_TIMEOUT_STEP;
                user_config_mark_dirty();
            }
            return false;
        case SLEEP_TIMEOUT_DEC:
            if (record->event.pressed && user_config.ee_sleep_timeout > SLEEP_TIMEOUT_MIN) {
                user_config.ee_sleep_timeout -= SLEEP_TIMEOUT_STEP;
                user_config_mark_dirty();
            }
            return false;

        case USB_SLEEP_TOGGLE:
            if (record->event.pressed) {
                set_f_usb_sleep_enable(!f_usb_sleep_enable);
                user_config_mark_dirty();
            }
            return false;

        case DEEP_SLEEP_TOGGLE:
            if (record->event.pressed) {
                set_f_deep_sleep_enable(!f_deep_sleep_enable);
                user_config_mark_dirty();
            }
            return false;

        case NKRO_MODE:
            if (record->event.pressed) {
                uint8_t mode = get_nkro_mode();
                mode = (mode + 1) % NKRO_MODE_COUNT; /* Auto -> On -> Off -> Auto */
                set_nkro_mode(mode);
                apply_nkro_override();
                user_config_mark_dirty();
            }
            return false;

        default:
            return true;
    }
}

/**
    @brief  timer process.
 */
void timer_pro(void) {
    static uint32_t interval_timer = 0;
    static bool     f_first        = true;

    if (f_first) {
        f_first        = false;
        interval_timer = timer_read32();
        m_host_driver  = host_get_driver();
    }

    /* Count elapsed steps so slower housekeeping loops don't stretch
     * timeouts.  Each step is 10 ms (TIMER_STEP). */
    uint32_t elapsed = timer_elapsed32(interval_timer);
    if (elapsed < TIMER_STEP_MS) return;

    uint32_t steps = elapsed / TIMER_STEP_MS;
    interval_timer += steps * TIMER_STEP_MS;

    if (rf_link_show_time < RF_LINK_SHOW_TIME) {
        uint32_t remaining = RF_LINK_SHOW_TIME - rf_link_show_time;
        rf_link_show_time += (steps < remaining) ? steps : remaining;
    }

    if (no_act_time < NO_ACT_TIME_MAX) {
        uint32_t remaining = NO_ACT_TIME_MAX - no_act_time;
        no_act_time += (steps < remaining) ? steps : remaining;
    }

    if (rf_linking_time < RF_LINKING_TIME_MAX) {
        uint32_t remaining = RF_LINKING_TIME_MAX - rf_linking_time;
        rf_linking_time += (steps < remaining) ? steps : remaining;
    }
}

/**
 * @brief  loading eeprom data.
 */
void m_loading_eeprom_data(void) {
    eeconfig_read_user_datablock(&user_config, 0, sizeof(user_config_t));
    if (user_config.default_brightness_flag != DEFAULT_BRIGHTNESS_FLAG) {
        rgb_matrix_sethsv(RGB_DEFAULT_COLOUR, 255, RGB_MATRIX_MAXIMUM_BRIGHTNESS - RGB_MATRIX_VAL_STEP * 2);
        user_config.default_brightness_flag = DEFAULT_BRIGHTNESS_FLAG;
        user_config.ee_side_led            = side_led_pack(side_mode_a, side_mode_b, side_rgb, side_colour, side_light, side_speed);
        user_config.ee_debounce_press_ms    = 5;
        user_config.ee_debounce_release_ms  = 5;
        user_config.ee_sleep_timeout        = SLEEP_TIMEOUT_DEFAULT;
        set_f_dev_sleep_enable(true);
        set_f_usb_sleep_enable(false);
        set_f_deep_sleep_enable(true);
        set_nkro_mode(NKRO_AUTO);
        user_config.ee_socd_mode = SOCD_DEFAULT_MODE;
        socd_set_mode(user_config.ee_socd_mode);
        eeconfig_update_user_datablock(&user_config, 0, sizeof(user_config_t));
    } else {
        side_mode_a = side_led_get_mode_a();
        side_mode_b = side_led_get_mode_b();
        side_light  = side_led_get_light();
        side_speed  = side_led_get_speed();
        side_rgb    = side_led_get_rgb();
        side_colour = side_led_get_colour();

        /* Clamp packed EEPROM values to valid lookup-table bounds.
         * 3-bit fields can hold 0-7, but the tables are smaller.
         *   mode_a: 0-4 (SIDE_WAVE..SIDE_STATIC)
         *   mode_b: 0-6 (SIDE_MODE_1..SIDE_MODE_7)
         *   light:  0-4 (side_light_table[5])
         *   speed:  0-4 (side_speed_table[5][5])
         *   colour: 0-7 (colour_lib[9]) */
        if (side_mode_a > 4) side_mode_a = 0;
        if (side_mode_b > 6) side_mode_b = 0;
        if (side_light > 4)  side_light = 2;
        if (side_speed > 4)  side_speed = 2;
        if (side_colour > 7) side_colour = 0;
        side_led_set_mode_a(side_mode_a);
        side_led_set_mode_b(side_mode_b);
        side_led_set_light(side_light);
        side_led_set_speed(side_speed);
        side_led_set_colour(side_colour);

        if (user_config.ee_debounce_press_ms == 0 || user_config.ee_debounce_press_ms > 99)
            user_config.ee_debounce_press_ms = 5;
        if (user_config.ee_debounce_release_ms == 0 || user_config.ee_debounce_release_ms > 99)
            user_config.ee_debounce_release_ms = 5;
        if (user_config.ee_sleep_timeout < SLEEP_TIMEOUT_MIN || user_config.ee_sleep_timeout > SLEEP_TIMEOUT_MAX)
            user_config.ee_sleep_timeout = SLEEP_TIMEOUT_DEFAULT;
        if (get_nkro_mode() > NKRO_OFF)
            set_nkro_mode(NKRO_AUTO);
        if (user_config.ee_socd_mode > SOCD_MODE_MAX)
            user_config.ee_socd_mode = SOCD_DEFAULT_MODE;
        socd_set_mode(user_config.ee_socd_mode);
    }
}

/**
   qmk keyboard pre init
 */
void keyboard_pre_init_kb(void) {
    rgb_driver_gpio_init();
    keyboard_pre_init_user();
}

/**
   qmk keyboard post init
 */
void keyboard_post_init_kb(void) {
    m_gpio_init();
    rf_uart_init();
    wait_ms(RF_INIT_DELAY_MS);
    rf_device_init();

    m_break_all_key();
    m_loading_eeprom_data();
    m_power_on_dial_sw_scan();
    keyboard_post_init_user();

    rf_link_show_time = 0;

#ifdef CONSOLE_ENABLE
    debug_enable = true;
#    ifdef RGB_DEBUG
    // LED-update logging mode: keep the console on but silence the HID/matrix
    // event spam so the side/status LED stream is readable.
    debug_matrix   = false;
    debug_keyboard = false;
    dprintf("DBG %s RGB debug console enabled\n", PRODUCT);
#    else
    debug_matrix   = true;
    debug_keyboard = true;
#        ifdef MOUSEKEY_ENABLE
    debug_mouse = true;
#        endif
    dprintf("DBG %s console enabled\n", PRODUCT);
    dprintf("rows=%d cols=%d diode=%s default_layer=%ld layer_state=%08lX\n",
            MATRIX_ROWS, MATRIX_COLS, DIODE_DIRECTION_STR,
            (uint32_t)default_layer_state, (uint32_t)layer_state);
#    endif
#endif
}

/**
   rgb_matrix_indicators_user
 */
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
#ifdef RGB_DEBUG
    // RGB debug mode: mask all normal effects and side shows, render only
    // the active debug program (battery simulator or LED stepper).
    extern void rgb_debug_render(void);
    rgb_debug_render();
    return true;
#else
    if (keymap_config.no_gui) {
        rgb_matrix_set_color(WIN_LOCK_LED_INDEX, 0x00, 0x80, 0x00);
    }

    // Side LEDs are driven by the dedicated side LED system in side.c,
    // not by RGB matrix effects.  Render them here — after the effect
    // has written the buffer but before the PWM flush — so the side
    // colours always win and don't flicker when a built-in effect
    // writes to all 128 LEDs.
    m_side_led_show();

    return true;
#endif
}

/**
 * @brief  Non-blocking device reset state machine.
 *
 * Replaces the blocking reset that used wait_ms(500) + blocking
 * uart_send_cmd ack-waits + a blocking 1.2 s LED blink loop.
 * Each state transitions on timer_elapsed32() so the main loop
 * (matrix scan, RF receive, sleep, dial scan) keeps running.
 */
static void dev_reset_task(void) {
    switch (dev_reset_state) {
        case RESET_IDLE:
            return;

        case RESET_SET_LINK:
            /* Queue CMD_SET_LINK non-blocking; the deferred task drains it. */
            uart_send_cmd_deferred(CMD_SET_LINK, 10);
            dev_reset_state = RESET_WAIT;
            dev_reset_timer = timer_read32();
            break;

        case RESET_WAIT:
            /* 500 ms cool-down so the RF module processes SET_LINK first. */
            if (timer_elapsed32(dev_reset_timer) >= RESET_WAIT_MS) {
                dev_reset_state = RESET_CLR_DEVICE;
            }
            break;

        case RESET_CLR_DEVICE:
            uart_send_cmd_deferred(CMD_CLR_DEVICE, 10);
            dev_reset_state = RESET_EECONFIG;
            break;

        case RESET_EECONFIG:
            eeconfig_init();
            /* Stop RGB matrix effects so they don't overwrite blink colors. */
            rgb_matrix_disable();
            dev_reset_blink    = 0;
            dev_reset_blink_on = true;
            rgb_matrix_set_color_all(0xFF, 0xFF, 0xFF);
            rgb_matrix_update_pwm_buffers();
            dev_reset_timer = timer_read32();
            dev_reset_state = RESET_BLINK;
            break;

        case RESET_BLINK:
            if (dev_reset_blink_on) {
                if (timer_elapsed32(dev_reset_timer) >= RESET_BLINK_MS) {
                    rgb_matrix_set_color_all(0x00, 0x00, 0x00);
                    rgb_matrix_update_pwm_buffers();
                    dev_reset_timer    = timer_read32();
                    dev_reset_blink_on = false;
                }
            } else {
                if (timer_elapsed32(dev_reset_timer) >= RESET_BLINK_MS) {
                    if (++dev_reset_blink >= RESET_BLINK_COUNT) {
                        dev_reset_state = RESET_INIT;
                        break;
                    }
                    rgb_matrix_set_color_all(0xFF, 0xFF, 0xFF);
                    rgb_matrix_update_pwm_buffers();
                    dev_reset_timer    = timer_read32();
                    dev_reset_blink_on = true;
                }
            }
            break;

        case RESET_INIT:
            device_reset_init();

            keymap_config.no_gui = 0;
            kbd_flags.win_lock        = 0;

            if (dev_info.sys_sw_state == SYS_SW_MAC) {
                default_layer_set(1 << 0); // MAC
                keymap_config.nkro = 0;
            } else {
                default_layer_set(1 << 2); // WIN
                keymap_config.nkro = 1;
            }

            apply_nkro_override();

            dev_reset_state = RESET_IDLE;
            break;
    }
}

/**
   housekeeping_task_kb
 */
void housekeeping_task_kb(void) {
    user_config_flush_if_dirty();
#ifdef CONSOLE_ENABLE
    debug_matrix_scan();
#endif
#ifdef RGB_DEBUG
    extern void rgb_debug_task(void);
    rgb_debug_task();
#endif
    timer_pro();

    uart_receive_pro();

    uart_send_cmd_deferred_task();

    macro_tap_task();

    uart_send_report_func();

    dev_sts_sync();

    long_press_key();

    dev_reset_task();

    dial_sw_scan();

    Sleep_Handle();
}

/* VIA custom value IDs for the Hardware settings tab. */
enum via_custom_value_id {
    id_debounce_press_ms   = 1,
    id_debounce_release_ms = 2,
    id_sleep_timeout       = 3,
    id_sleep_toggle        = 4,
    id_usb_sleep_toggle    = 5,
    id_deep_sleep_toggle   = 6,
    id_nkro_mode           = 7,
    id_socd_mode           = 8,
    id_battery_level       = 9,
};

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    /* data = [ command_id, channel_id, value_id, value_data... ] */
    uint8_t *command_id = &(data[0]);
    uint8_t *channel_id = &(data[1]);
    uint8_t *value_id   = &(data[2]);
    uint8_t *value_data = &(data[3]);

    /* Need at least command + channel + value_id + 1 data byte. */
    if (length < 4) {
        *command_id = id_unhandled;
        return;
    }

    if (*channel_id != id_custom_channel) {
        *command_id = id_unhandled;
        return;
    }

    switch (*command_id) {
        case id_custom_set_value:
            switch (*value_id) {
                case id_debounce_press_ms:
                    user_config.ee_debounce_press_ms = (value_data[0] < 1) ? 1 : value_data[0];
                    user_config_mark_dirty();
                    break;
                case id_debounce_release_ms:
                    user_config.ee_debounce_release_ms = (value_data[0] < 1) ? 1 : value_data[0];
                    user_config_mark_dirty();
                    break;
                case id_sleep_timeout:
                    user_config.ee_sleep_timeout = (value_data[0] < SLEEP_TIMEOUT_MIN) ? SLEEP_TIMEOUT_MIN :
                                                   (value_data[0] > SLEEP_TIMEOUT_MAX) ? SLEEP_TIMEOUT_MAX : value_data[0];
                    user_config_mark_dirty();
                    break;
                case id_sleep_toggle:
                    set_f_dev_sleep_enable(value_data[0] ? 1 : 0);
                    user_config_mark_dirty();
                    break;
                case id_usb_sleep_toggle:
                    set_f_usb_sleep_enable(value_data[0] ? 1 : 0);
                    user_config_mark_dirty();
                    break;
                case id_deep_sleep_toggle:
                    set_f_deep_sleep_enable(value_data[0] ? 1 : 0);
                    user_config_mark_dirty();
                    break;
                case id_nkro_mode:
                    set_nkro_mode(value_data[0] > NKRO_OFF ? NKRO_AUTO : value_data[0]);
                    apply_nkro_override();
                    user_config_mark_dirty();
                    break;
                case id_socd_mode:
                    user_config.ee_socd_mode = (value_data[0] > SOCD_MODE_MAX) ? SOCD_OFF : value_data[0];
                    socd_set_mode(user_config.ee_socd_mode);
                    user_config_mark_dirty();
                    break;
                /* id_battery_level is read-only — no set handler. */
                default:
                    *command_id = id_unhandled;
                    break;
            }
            break;

        case id_custom_get_value:
            switch (*value_id) {
                case id_debounce_press_ms:
                    value_data[0] = user_config.ee_debounce_press_ms;
                    break;
                case id_debounce_release_ms:
                    value_data[0] = user_config.ee_debounce_release_ms;
                    break;
                case id_sleep_timeout:
                    value_data[0] = user_config.ee_sleep_timeout;
                    break;
                case id_sleep_toggle:
                    value_data[0] = f_dev_sleep_enable ? 1 : 0;
                    break;
                case id_usb_sleep_toggle:
                    value_data[0] = f_usb_sleep_enable ? 1 : 0;
                    break;
                case id_deep_sleep_toggle:
                    value_data[0] = f_deep_sleep_enable ? 1 : 0;
                    break;
                case id_nkro_mode:
                    value_data[0] = get_nkro_mode();
                    break;
                case id_socd_mode:
                    value_data[0] = socd_get_mode();
                    break;
                case id_battery_level:
                    value_data[0] = dev_info.rf_battery;
                    break;
                default:
                    *command_id = id_unhandled;
                    break;
            }
            break;

        case id_custom_save:
            eeconfig_update_user_datablock(&user_config, 0, sizeof(user_config_t));
            user_config_dirty = false;
            break;

        default:
            *command_id = id_unhandled;
            break;
    }
}
