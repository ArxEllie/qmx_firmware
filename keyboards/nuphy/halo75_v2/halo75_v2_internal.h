// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "halo75_v2.h"

/* ── Shared variables ──────────────────────────────────────────── */

/* Defined in halo75_v2.c */
extern DEV_INFO_STRUCT dev_info;
extern host_driver_t  *m_host_driver;
extern uint8_t         host_mode;
extern uint16_t        rf_linking_time;
extern uint16_t        rf_link_show_time;
extern uint8_t         rf_blink_cnt;
extern uint32_t        no_act_time;
extern bool            f_uart_ack;
extern bool            f_bat_hold;
extern bool            f_rf_read_data_ok;
extern bool            f_rf_sts_sysc_ok;
extern bool            f_rf_new_adv_ok;
extern bool            f_rf_reset;
extern bool            f_rf_hand_ok;
extern bool            f_send_channel;
extern bool            f_wakeup_prepare;
extern bool            f_dial_sw_init_ok;
extern bool            f_goto_sleep;

/* EEPROM write batching — defined in halo75_v2.c */
void     user_config_mark_dirty(void);
void     user_config_flush_if_dirty(void);

/* Defined in rf.c */
extern uint8_t         uart_bit_report_buf[32];
extern uint8_t         bitkb_report_buf[32];
extern uint8_t         bytekb_report_buf[8];

/* Defined in side.c */
extern uint8_t         side_mode_a;
extern uint8_t         side_mode_b;
extern uint8_t         side_light;
extern uint8_t         side_speed;
extern uint8_t         side_rgb;
extern uint8_t         side_colour;
extern bool            low_bat_flag;

/* ── Shared functions ──────────────────────────────────────────── */

/* Defined in rf.c */
void     m_break_all_key(void);
void     uart_send_report_keyboard(report_keyboard_t *report);
void     uart_send_report_nkro(report_nkro_t *report);
void     uart_send_mouse_report(report_mouse_t *report);
void     uart_send_consumer_report(report_extra_t *report);
void     uart_send_system_report(report_extra_t *report);
uint8_t  uart_send_cmd_deferred(uint8_t cmd, uint8_t delayms);
uint8_t  uart_send_cmd(uint8_t cmd, uint8_t wait_ack, uint8_t delayms);
void     uart_send_cmd_deferred_task(void);
void     uart_send_report(uint8_t report_type, const uint8_t *report_buf, uint8_t report_size);
void     uart_receive_pro(void);
void     uart_send_report_func(void);
void     dev_sts_sync(void);
void     rf_device_init(void);
void     rf_uart_init(void);

/* Defined in sleep.c */
void     Sleep_Handle(void);

/* Defined in side.c */
void     m_side_led_show(void);
void     device_reset_init(void);
void     light_level_control(uint8_t brighten);
void     light_speed_control(uint8_t fast);
void     side_colour_control(uint8_t dir);
void     side_mode_a_control(uint8_t dir);
void     side_mode_b_control(uint8_t dir);
