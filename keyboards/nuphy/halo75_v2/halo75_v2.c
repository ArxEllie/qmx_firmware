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
#include "usb_main.h"
#include "rf_driver.h"
#include "i2c_master.h"
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
    .rf_baterry = 100,
    .link_mode  = LINK_USB,
    .rf_state   = RF_IDLE,
};

uint16_t       rf_linking_time       = 0;
uint16_t       rf_link_show_time     = 0;
uint8_t        rf_blink_cnt          = 0;
uint16_t       no_act_time           = 0;
host_driver_t *m_host_driver         = 0;
uint16_t       dev_reset_press_delay = 0;
uint16_t       rf_sw_press_delay     = 0;
uint8_t        rf_sw_temp            = 0;
uint8_t        host_mode;

extern uint8_t            side_mode_a;
extern uint8_t            side_light;
extern uint8_t            side_speed;
extern uint8_t            side_rgb;
extern uint8_t            side_colour;
extern report_keyboard_t *keyboard_report;
extern report_nkro_t     *nkro_report;
extern uint8_t            side_mode_b;
extern uint8_t            uart_bit_report_buf[32];
extern uint8_t            bitkb_report_buf[32];
extern uint8_t            bytekb_report_buf[8];

bool f_uart_ack        = 0;
bool f_bat_show        = 0;
bool f_bat_hold        = 0;
bool f_chg_show        = 1;
bool f_sys_show        = 0;
bool f_sleep_show      = 0;
bool f_usb_offline     = 0;
bool f_rf_read_data_ok = 0;
bool f_rf_sts_sysc_ok  = 0;
bool f_rf_new_adv_ok   = 0;
bool f_rf_reset        = 0;
bool f_send_channel    = 0;
bool f_rf_hand_ok      = 0;
bool f_rf_send_bitkb   = 0;
bool f_rf_send_byte    = 0;
bool f_rf_send_consume = 0;
bool f_wakeup_prepare  = 0;
bool f_dial_sw_init_ok = 0;
bool f_goto_sleep      = 0;
bool f_rf_sw_press     = 0;
bool f_dev_reset_press = 0;
bool f_win_lock        = 0;

void    rf_device_init(void);
void    rf_uart_init(void);
void    m_side_led_show(void);
void    dev_sts_sync(void);
void    uart_receive_pro(void);
void    Sleep_Handle(void);
void    uart_send_report_func(void);
uint8_t uart_send_cmd(uint8_t cmd, uint8_t ack_cnt, uint8_t delayms);
uint8_t uart_send_cmd_deferred(uint8_t cmd, uint8_t delayms);
void    uart_send_cmd_deferred_task(void);
void    uart_send_report(uint8_t report_type, uint8_t *report_buf, uint8_t report_size);
void    device_reset_show(void);
void    device_reset_init(void);
void    m_deinit_usb_072(void);

extern void light_speed_control(uint8_t fast);
extern void light_level_control(uint8_t brighten);
extern void side_colour_control(uint8_t dir);
extern void side_mode_a_control(uint8_t dir);
extern void side_mode_b_control(uint8_t dir);
extern bool low_bat_flag;

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
    wait_ms(50);
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

    if (timer_elapsed32(long_press_timer) < 100) return;
    long_press_timer = timer_read32();

    if (new_adv_retry) {
        if (f_rf_new_adv_ok) {
            new_adv_retry = 0;
        } else {
            uart_send_cmd_deferred(CMD_NEW_ADV, 1);
            new_adv_retry--;
        }
    }

    if (f_rf_sw_press) {
        rf_sw_press_delay++;
        if (rf_sw_press_delay >= RF_LONG_PRESS_DELAY) {
            f_rf_sw_press        = 0;
            dev_info.link_mode   = rf_sw_temp;
            dev_info.rf_channel  = rf_sw_temp;
            dev_info.ble_channel = rf_sw_temp;
            f_rf_new_adv_ok      = 0;
            new_adv_retry        = 5;
        }
    } else {
        rf_sw_press_delay = 0;
    }

    if (f_dev_reset_press) {
        dev_reset_press_delay++;
        if (dev_reset_press_delay >= DEV_RESET_PRESS_DELAY) {
            f_dev_reset_press = 0;

            if (dev_info.link_mode != LINK_USB) {
                if (dev_info.link_mode != LINK_RF_24) {
                    dev_info.link_mode   = LINK_BT_1;
                    dev_info.ble_channel = LINK_BT_1;
                    dev_info.rf_channel  = LINK_BT_1;
                }
            } else {
                dev_info.ble_channel = LINK_BT_1;
            }

            uart_send_cmd(CMD_SET_LINK, 10, 10);
            wait_ms(500);
            uart_send_cmd(CMD_CLR_DEVICE, 10, 10);

            eeconfig_init();
            device_reset_show();
            device_reset_init();

            keymap_config.no_gui = 0;
            f_win_lock           = 0;

            if (dev_info.sys_sw_state == SYS_SW_MAC) {
                default_layer_set(1 << 0); // MAC
                keymap_config.nkro = 0;
            } else {
                default_layer_set(1 << 2); // WIN
                keymap_config.nkro = 1;
            }
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
    f_send_channel     = 1;

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
        if (timer_elapsed32(dial_scan_timer) < 20) return;
    }
    dial_scan_timer = timer_read32();

    gpio_set_pin_input_high(DEV_MODE_PIN);
    gpio_set_pin_input_high(SYS_MODE_PIN);

    if (gpio_read_pin(DEV_MODE_PIN)) dial_scan |= 0X01;
    if (gpio_read_pin(SYS_MODE_PIN)) dial_scan |= 0X02;

    if (dial_save != dial_scan) {
        if (++dial_change_cnt < 3) return;
        dial_change_cnt = 0;
        m_break_all_key();
        dial_save         = dial_scan;
        no_act_time       = 0;
        rf_linking_time   = 0;
        debounce          = 25;
        f_dial_sw_init_ok = 0;
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
            f_sys_show = 1;
            default_layer_set(1 << 2);
            dev_info.sys_sw_state = SYS_SW_WIN;
            keymap_config.no_gui  = f_win_lock;
            m_break_all_key();
        }
        keymap_config.nkro = 1;
    } else {
        if (dev_info.sys_sw_state != SYS_SW_MAC) {
            f_sys_show = 1;
            default_layer_set(1 << 0);
            dev_info.sys_sw_state = SYS_SW_MAC;
            f_win_lock            = keymap_config.no_gui;
            m_break_all_key();
        }
        keymap_config.nkro   = 0;
        keymap_config.no_gui = 0;
    }

    if (f_dial_sw_init_ok == 0) {
        f_dial_sw_init_ok = 1;
        flag_power_on     = 0;

        if (dev_info.link_mode != LINK_USB) {
            host_set_driver(&rf_host_driver);
        }
    }
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

    f_win_lock = 0;

    gpio_set_pin_input_high(DEV_MODE_PIN);
    gpio_set_pin_input_high(SYS_MODE_PIN);

    for (debounce = 0; debounce < 10; debounce++) {
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
            f_win_lock            = keymap_config.no_gui;
            keymap_config.no_gui  = 0;
            m_break_all_key();
        }
    }
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
    if (macro_tap_keycode != KC_NO && timer_elapsed32(macro_tap_timer) >= 20) {
        macro_tap_release();
    }
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
    switch (keycode) {
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

        case LNK_RF:
            if (record->event.pressed) {
                if (dev_info.link_mode != LINK_USB) {
                    rf_sw_temp    = LINK_RF_24;
                    f_rf_sw_press = 1;
                    m_break_all_key();
                }
            } else if (f_rf_sw_press) {
                f_rf_sw_press = 0;
                if (rf_sw_press_delay < RF_LONG_PRESS_DELAY) {
                    dev_info.link_mode   = rf_sw_temp;
                    dev_info.rf_channel  = rf_sw_temp;
                    dev_info.ble_channel = rf_sw_temp;
                    uart_send_cmd_deferred(CMD_SET_LINK, 20);
                }
            }
            return false;

        case LNK_BLE1:
            if (record->event.pressed) {
                if (dev_info.link_mode != LINK_USB) {
                    rf_sw_temp    = LINK_BT_1;
                    f_rf_sw_press = 1;
                    m_break_all_key();
                }
            } else if (f_rf_sw_press) {
                f_rf_sw_press = 0;
                if (rf_sw_press_delay < RF_LONG_PRESS_DELAY) {
                    dev_info.link_mode   = rf_sw_temp;
                    dev_info.rf_channel  = rf_sw_temp;
                    dev_info.ble_channel = rf_sw_temp;
                    uart_send_cmd_deferred(CMD_SET_LINK, 20);
                }
            }
            return false;

        case LNK_BLE2:
            if (record->event.pressed) {
                if (dev_info.link_mode != LINK_USB) {
                    rf_sw_temp    = LINK_BT_2;
                    f_rf_sw_press = 1;
                    m_break_all_key();
                }
            } else if (f_rf_sw_press) {
                f_rf_sw_press = 0;
                if (rf_sw_press_delay < RF_LONG_PRESS_DELAY) {
                    dev_info.link_mode   = rf_sw_temp;
                    dev_info.rf_channel  = rf_sw_temp;
                    dev_info.ble_channel = rf_sw_temp;
                    uart_send_cmd_deferred(CMD_SET_LINK, 20);
                }
            }
            return false;

        case LNK_BLE3:
            if (record->event.pressed) {
                if (dev_info.link_mode != LINK_USB) {
                    rf_sw_temp    = LINK_BT_3;
                    f_rf_sw_press = 1;
                    m_break_all_key();
                }
            } else if (f_rf_sw_press) {
                f_rf_sw_press = 0;
                if (rf_sw_press_delay < RF_LONG_PRESS_DELAY) {
                    dev_info.link_mode   = rf_sw_temp;
                    dev_info.rf_channel  = rf_sw_temp;
                    dev_info.ble_channel = rf_sw_temp;
                    uart_send_cmd_deferred(CMD_SET_LINK, 20);
                }
            }
            return false;

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

        case SIDE_VAI:
            if (record->event.pressed) {
                if (low_bat_flag && (side_light == 1)) return false;
                light_level_control(1);
            }
            return false;

        case SIDE_VAD:
            if (record->event.pressed) {
                light_level_control(0);
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

        case DEV_RESET:
            if (record->event.pressed) {
                f_dev_reset_press = 1;
                m_break_all_key();
            } else {
                f_dev_reset_press = 0;
            }
            return false;

        case SLEEP_MODE:
            if (record->event.pressed) {
                if (f_dev_sleep_enable)
                    f_dev_sleep_enable = false;
                else
                    f_dev_sleep_enable = true;
                f_sleep_show = 1;
                eeconfig_update_user_datablock(&user_config, 0, sizeof(user_config_t));
            }
            return false;

        case BAT_SHOW:
            if (record->event.pressed) {
                f_bat_hold = !f_bat_hold;
            }
            return false;

        default:
            return true;
    }
    return true;
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

    if (timer_elapsed32(interval_timer) < 10) {
        return;
    } else if (timer_elapsed32(interval_timer) > 20) {
        interval_timer = timer_read32();
    } else {
        interval_timer += 10;
    }

    if (rf_link_show_time < RF_LINK_SHOW_TIME) rf_link_show_time++;

    if (no_act_time < 0xffff) no_act_time++;

    if (rf_linking_time < 0xffff) rf_linking_time++;
}

/**
 * @brief  londing eeprom data.
 */
void m_londing_eeprom_data(void) {
    eeconfig_read_user_datablock(&user_config, 0, sizeof(user_config_t));
    if (user_config.default_brightness_flag != 0xA5) {
        rgb_matrix_sethsv(RGB_DEFAULT_COLOUR, 255, RGB_MATRIX_MAXIMUM_BRIGHTNESS - RGB_MATRIX_VAL_STEP * 2);
        user_config.default_brightness_flag = 0xA5;
        user_config.ee_side_mode_a          = side_mode_a;
        user_config.ee_side_mode_b          = side_mode_b;
        user_config.ee_side_light           = side_light;
        user_config.ee_side_speed           = side_speed;
        user_config.ee_side_rgb             = side_rgb;
        user_config.ee_side_colour          = side_colour;
        f_dev_sleep_enable                  = true;
        eeconfig_update_user_datablock(&user_config, 0, sizeof(user_config_t));
    } else {
        side_mode_a = user_config.ee_side_mode_a;
        side_mode_b = user_config.ee_side_mode_b;
        side_light  = user_config.ee_side_light;
        side_speed  = user_config.ee_side_speed;
        side_rgb    = user_config.ee_side_rgb;
        side_colour = user_config.ee_side_colour;
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
    wait_ms(500);
    rf_device_init();

    m_break_all_key();
    m_londing_eeprom_data();
    m_power_on_dial_sw_scan();
    keyboard_post_init_user();

    rf_link_show_time = 0;

#ifdef CONSOLE_ENABLE
    debug_enable   = true;
    debug_matrix   = true;
    debug_keyboard = true;
#    ifdef MOUSEKEY_ENABLE
    debug_mouse = true;
#    endif
    dprintf("DBG %s console enabled\n", PRODUCT);
    dprintf("rows=%d cols=%d diode=%s default_layer=%ld layer_state=%08lX\n",
            MATRIX_ROWS, MATRIX_COLS, DIODE_DIRECTION_STR,
            (uint32_t)default_layer_state, (uint32_t)layer_state);
#endif
}

/**
   rgb_matrix_indicators_user
 */
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (keymap_config.no_gui) {
        rgb_matrix_set_color(72, 0x00, 0x80, 0x00);
    }

    // Side LEDs are driven by the dedicated side LED system in side.c,
    // not by RGB matrix effects.  Render them here — after the effect
    // has written the buffer but before the PWM flush — so the side
    // colours always win and don't flicker when a built-in effect
    // writes to all 128 LEDs.
    m_side_led_show();

    return true;
}

/**
   housekeeping_task_kb
 */
void housekeeping_task_kb(void) {
#ifdef CONSOLE_ENABLE
    debug_matrix_scan();
#endif
    timer_pro();

    uart_receive_pro();

    uart_send_cmd_deferred_task();

    macro_tap_task();

    uart_send_report_func();

    dev_sts_sync();

    long_press_key();

    dial_sw_scan();

    Sleep_Handle();
}
