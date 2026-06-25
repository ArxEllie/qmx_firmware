// Copyright 2023 Persama (@Persama)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "quantum.h"

enum custom_keycodes {
    RF_DFU = QK_KB_0,
    LNK_USB,
    LNK_RF,
    LNK_BLE1,
    LNK_BLE2,
    LNK_BLE3,

    MAC_TASK,
    MAC_SEARCH,
    MAC_VOICE,
    MAC_CONSOLE,
    MAC_DND,
    MAC_PRT,
    MAC_PRTA,

    DEV_RESET,
    SLEEP_MODE,
    BAT_SHOW,

    SIDE_VAI,
    SIDE_VAD,
    SIDE_MOD_A,
    SIDE_MOD_B,
    SIDE_HUI,
    SIDE_SPI,
    SIDE_SPD,

    DEBOUNCE_PRESS_DEC,
    DEBOUNCE_PRESS_INC,
    DEBOUNCE_PRESS_SHOW,
    DEBOUNCE_RELEASE_DEC,
    DEBOUNCE_RELEASE_INC,
    DEBOUNCE_RELEASE_SHOW,

    SLEEP_TIMEOUT_DEC,
    SLEEP_TIMEOUT_INC,
    USB_SLEEP_TOGGLE,
    DEEP_SLEEP_TOGGLE,
    NKRO_MODE,
};

extern uint8_t m_sleep_led;

typedef enum {
    RX_Idle,
    RX_Receiving,
    RX_Done,
    RX_Fail,
    RX_OV_ERR,
    RX_SUM_ERR,
    RX_CMD_ERR,
    RX_DATA_ERR,
    RX_DATA_OV,
    RX_FORMAT_ERR,

    TX_OK = 0XE0,
    TX_DONE,
    TX_BUSY,
    TX_TIMEOUT,
    TX_DATA_ERR,

} TYPE_RX_STATE;

#define FUNC_VALID_LEN 32
#define UART_HEAD 0x5A

#define RF_IDLE 0
#define RF_PAIRING 1
#define RF_LINKING 2
#define RF_CONNECT 3
#define RF_DISCONNECT 4
#define RF_SLEEP 5
#define RF_SNIF 6
#define RF_INVALID 0XFE
#define RF_ERR_STATE 0XFF

#define RF_LONG_PRESS_DELAY 30
#define DEV_RESET_PRESS_DELAY 30

/* ── Semantically-named timing & threshold constants ────────────── */

/* Main loop / polling intervals (ms) */
#define TIMER_STEP_MS           10    /* timer_pro() tick interval */
#define LONG_PRESS_POLL_MS      100   /* long_press_key() poll interval */
#define DIAL_SCAN_INTERVAL_MS   20    /* dial_sw_scan() poll interval */
#define MACRO_TAP_HOLD_MS       20    /* macro_tap_task() hold before auto-release */

/* EEPROM batching */
#define EEPROM_FLUSH_DELAY_MS   500   /* dirty settle time before auto-flush */

/* Dial switch debouncing */
#define DIAL_DEBOUNCE_COUNT     25    /* debounce steps after a dial change */
#define DIAL_CHANGE_CONFIRM     3     /* consecutive stable reads to accept change */
#define DIAL_POWER_ON_DEBOUNCE  10    /* power-on dial scan debounce iterations */

/* Device reset state machine */
#define RESET_WAIT_MS           500   /* cool-down after CMD_SET_LINK */
#define RESET_BLINK_MS          200   /* per-blink duration (white/off) */
#define RESET_BLINK_COUNT       3     /* total white/off blinks */

/* RF / NRF init delays */
#define NRF_RESET_DELAY_MS      50    /* NRF reset pulse width */
#define RF_INIT_DELAY_MS        500   /* post-UART-init delay before rf_device_init */

/* Timer saturation limits */
#define NO_ACT_TIME_MAX         0xFFFFFFU /* no_act_time ceiling (24-bit) */
#define RF_LINKING_TIME_MAX     0xFFFFU   /* rf_linking_time ceiling (16-bit) */

/* EEPROM sentinel */
#define DEFAULT_BRIGHTNESS_FLAG 0xA6  /* first-boot marker for user_config */

/* NKRO mode cycling */
#define NKRO_MODE_COUNT         3     /* Auto -> On -> Off -> Auto */

/* LED index for Win lock indicator */
#define WIN_LOCK_LED_INDEX      72

#define CMD_POWER_UP 0XF0
#define CMD_SLEEP 0XF1
#define CMD_HAND 0XF2
#define CMD_SNIF 0XF3
#define CMD_24G_SUSPEND 0XF4
#define CMD_IDLE_EXIT 0XFE

#define CMD_RPT_MS 0XE0
#define CMD_RPT_BYTE_KB 0XE1
#define CMD_RPT_BIT_KB 0XE2
#define CMD_RPT_CONSUME 0XE3
#define CMD_RPT_SYS 0XE4

#define CMD_SET_LINK 0XC0
#define CMD_SET_CONFIG 0XC1
#define CMD_GET_CONFIG 0XC2
#define CMD_SET_NAME 0XC3
#define CMD_GET_NAME 0XC4
#define CMD_CLR_DEVICE 0XC5
#define CMD_NEW_ADV 0XC7
#define CMD_RF_STS_SYSC 0XC9
#define CMD_SET_24G_NAME 0XCA
#define CMD_GO_TEST 0XCF

#define CMD_RF_DFU 0XB1

#define CMD_WRITE_DATA 0X80
#define CMD_READ_DATA 0X81

#define CMD_WBAT_CFG 0X82
#define CMD_RBAT_CFG 0X83

#define LINK_RF_24 0
#define LINK_BT_1 1
#define LINK_BT_2 2
#define LINK_BT_3 3
#define LINK_USB 4

#define UART_MAX_LEN 64
typedef struct {
    uint8_t RXDState;
    uint8_t RXDLen;
    uint8_t RXDOverTime;
    uint8_t TXDLenBack;
    uint8_t TXDOffset;
    uint8_t TXDBuf[UART_MAX_LEN];
    uint8_t RXDBuf[UART_MAX_LEN];
} USART_MGR_STRUCT;

typedef struct {
    uint8_t link_mode;
    uint8_t rf_channel;
    uint8_t ble_channel;
    uint8_t rf_state;
    uint8_t rf_charge;
    uint8_t rf_led;
    uint8_t rf_baterry;
    uint8_t sys_sw_state;
} DEV_INFO_STRUCT;

/* Consolidated global boolean flags — previously scattered as individual
 * `bool f_xxx` globals.  Grouped here for discoverability and to keep
 * the .bss footprint tight.  Field names drop the legacy `f_` prefix;
 * access via `kbd_flags.xxx`. */
typedef struct {
    /* UART / RF protocol state */
    bool uart_ack;
    bool rf_hand_ok;
    bool rf_read_data_ok;
    bool rf_sts_sysc_ok;
    bool rf_new_adv_ok;
    bool rf_reset;
    bool send_channel;

    /* RF report dispatch */
    bool rf_send_bitkb;
    bool rf_send_byte;
    bool rf_send_consume;

    /* Power / sleep */
    bool goto_sleep;
    bool wakeup_prepare;
    bool usb_offline;

    /* Dial switch */
    bool dial_sw_init_ok;

    /* User interaction */
    bool rf_sw_press;
    bool dev_reset_press;
    bool win_lock;

    /* LED / display indicators */
    bool bat_show;
    bool bat_hold;
    bool chg_show;
    bool sys_show;
    bool sleep_show;
} keyboard_flags_t;

extern keyboard_flags_t kbd_flags;

#define DELAY_2MS 2
#define DELAY_4MS 4
#define DELAY_5MS 5
#define DELAY_6MS 6
#define DELAY_8MS 8
#define DELAY_10MS 10
#define DELAY_15MS 15
#define DELAY_20MS 20
#define DELAY_30MS 30
#define DELAY_40MS 40
#define DELAY_50MS 50
#define DELAY_100MS 100
#define DELAY_200MS 200
#define DELAY_300MS 300
#define DELAY_400MS 400
#define DELAY_500MS 500
#define DELAY_800MS 800
#define DELAY_1SEC 1000
#define DELAY_2SEC 2000
#define DELAY_3SEC 3000
#define DELAY_4SEC 4000
#define DELAY_5SEC 5000

#define SYS_SW_WIN 0xa1
#define SYS_SW_MAC 0xa2

#define RF_LINK_SHOW_TIME 300

#define HOST_USB_TYPE 0
#define HOST_BLE_TYPE 1
#define HOST_RF_TYPE 2

#define LINK_TIMEOUT (uint32_t)(100 * 120)
#define SLEEP_TIME_DELAY (uint32_t)(100 * 360)
#define POWER_DOWN_DELAY (uint16_t)(24)

#define SLEEP_TIMEOUT_DEFAULT 30   /* minutes */
#define SLEEP_TIMEOUT_MIN 1
#define SLEEP_TIMEOUT_MAX 60
#define SLEEP_TIMEOUT_STEP 1

/* TIMER_STEP is 50ms (Sleep_Handle runs every 50ms). */
#define SLEEP_TIMEOUT_TO_TICKS(min) ((uint32_t)(min) * 60 * 1000 / 50)

/* Packed side LED settings in ee_side_led (uint16_t):
 * bits 0-2:   mode_a (0-4, 5 modes)
 * bits 3-5:   mode_b (0-6, 7 modes)
 * bit  6:     rgb (0-1, rainbow vs fixed)
 * bits 7-9:   colour (0-7, 8 colours)
 * bits 10-12: light (0-4, 5 brightness levels)
 * bits 13-15: speed (0-4, 5 speed levels)
 */
#define side_led_get_mode_a()  (user_config.ee_side_led & 0x0007)
#define side_led_get_mode_b()  ((user_config.ee_side_led >> 3) & 0x0007)
#define side_led_get_rgb()     ((user_config.ee_side_led >> 6) & 0x0001)
#define side_led_get_colour()  ((user_config.ee_side_led >> 7) & 0x0007)
#define side_led_get_light()   ((user_config.ee_side_led >> 10) & 0x0007)
#define side_led_get_speed()   ((user_config.ee_side_led >> 13) & 0x0007)

#define side_led_set_mode_a(v)  (user_config.ee_side_led = (user_config.ee_side_led & ~0x0007) | ((uint16_t)(v) << 0))
#define side_led_set_mode_b(v)  (user_config.ee_side_led = (user_config.ee_side_led & ~0x0038) | ((uint16_t)(v) << 3))
#define side_led_set_rgb(v)     (user_config.ee_side_led = (user_config.ee_side_led & ~0x0040) | ((uint16_t)(v) << 6))
#define side_led_set_colour(v)  (user_config.ee_side_led = (user_config.ee_side_led & ~0x0380) | ((uint16_t)(v) << 7))
#define side_led_set_light(v)   (user_config.ee_side_led = (user_config.ee_side_led & ~0x1C00) | ((uint16_t)(v) << 10))
#define side_led_set_speed(v)   (user_config.ee_side_led = (user_config.ee_side_led & ~0xE000) | ((uint16_t)(v) << 13))

#define side_led_pack(ma, mb, rgb, col, light, spd) \
    ((uint16_t)((ma) & 0x7) | ((uint16_t)((mb) & 0x7) << 3) | \
     ((uint16_t)((rgb) & 0x1) << 6) | ((uint16_t)((col) & 0x7) << 7) | \
     ((uint16_t)((light) & 0x7) << 10) | ((uint16_t)((spd) & 0x7) << 13))

typedef struct __attribute__((packed)) {
    uint8_t  default_brightness_flag;
    uint16_t ee_side_led;           /* packed side LED settings */
    uint8_t  ee_debounce_press_ms;
    uint8_t  ee_debounce_release_ms;
    uint8_t  ee_sleep_timeout;      /* minutes, 1-60 */
    uint8_t  ee_dev_config;       /* bitfield: sleep/usb/deep sleep flags + NKRO mode */
    uint8_t  ee_socd_mode;          /* SOCD resolution mode (0=off, 1=neutral, 2=last-wins, 3=first-wins) */
} user_config_t;

extern user_config_t user_config;

/* ee_dev_config bit layout:
 *   bit 0: f_dev_sleep_enable
 *   bit 1: f_usb_sleep_enable
 *   bit 2: f_deep_sleep_enable
 *   bits 4-5: NKRO override mode (0=Auto, 1=On, 2=Off) */
#define f_dev_sleep_enable    (user_config.ee_dev_config & 0x01)
#define f_usb_sleep_enable    (user_config.ee_dev_config & 0x02)
#define f_deep_sleep_enable   (user_config.ee_dev_config & 0x04)

#define set_f_dev_sleep_enable(v)  do { if (v) user_config.ee_dev_config |= 0x01; else user_config.ee_dev_config &= ~0x01; } while (0)
#define set_f_usb_sleep_enable(v)  do { if (v) user_config.ee_dev_config |= 0x02; else user_config.ee_dev_config &= ~0x02; } while (0)
#define set_f_deep_sleep_enable(v) do { if (v) user_config.ee_dev_config |= 0x04; else user_config.ee_dev_config &= ~0x04; } while (0)

/* NKRO override modes: 0=Auto (follow OS switch), 1=On, 2=Off */
enum nkro_mode {
    NKRO_AUTO = 0,
    NKRO_ON   = 1,
    NKRO_OFF  = 2,
};
#define get_nkro_mode()  ((uint8_t)((user_config.ee_dev_config >> 4) & 0x03))
#define set_nkro_mode(m) do { user_config.ee_dev_config = (user_config.ee_dev_config & ~0x30) | (((uint8_t)(m) & 0x03) << 4); } while (0)

/* Shared side LED constants */
#define SIDE_INDEX 83
extern uint8_t f_side_flag;

/* Variant-specific side LED configuration */
#if defined(KEYBOARD_nuphy_halo75_v2_ansi)
#    define SIDE_LED_COUNT 45
#    define SIDE_RIM_START 0
extern const uint8_t side_led_index_tab[45];
uint8_t              is_side_rgb_on(uint8_t index);
#    define side_bat_led_set(idx, r, g, b) rgb_matrix_set_color(SIDE_INDEX + (idx), r, g, b)
#    define side_rgb_off(idx) rgb_matrix_set_color(side_led_index_tab[(idx)], 0, 0, 0)
#elif defined(KEYBOARD_nuphy_halo75_v2_iso)
#    define SIDE_LED_COUNT 44
#    define SIDE_RIM_START 5
extern const uint8_t side_led_index_tab[44];
uint8_t              is_side_rgb_on(uint8_t index);
/* ISO keeps the five status LEDs at table indices 0-4; battery indicators use
 * those logical indices directly, while rim animations start at index 5. */
#    define side_bat_led_set(idx, r, g, b) rgb_matrix_set_color(side_led_index_tab[(idx)], r, g, b)
#    define side_rgb_off(idx) rgb_matrix_set_color(side_led_index_tab[(idx)], 0, 0, 0)
#endif

/* Animation loop upper bound: ANSI table interleaves 5 status LEDs in the
 * middle, so the wave/new/spectrum/breathe loops stop 5 short of the end.
 * ISO keeps status LEDs at the head, so those loops run to the last rim LED. */
#if defined(KEYBOARD_nuphy_halo75_v2_ansi)
#    define SIDE_ANIM_LOOP_END (SIDE_LED_COUNT - 5)
#elif defined(KEYBOARD_nuphy_halo75_v2_iso)
#    define SIDE_ANIM_LOOP_END (SIDE_LED_COUNT - 1)
#endif
