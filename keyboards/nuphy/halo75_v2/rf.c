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
#include "uart.h" // qmk uart.h
#include "rf_driver.h"

/* RF report / sync timing (ms) */
#define RF_REPORT_INTERVAL_MS   300   /* periodic RF keyboard report push */
#define RF_IDLE_THRESHOLD       2000  /* no_act_time (steps) before suppressing reports */
#define RF_SYNC_INTERVAL_MS     200   /* dev_sts_sync poll interval */

/* NRF reset sequence (ms) */
#define NRF_RESET_LOW_MS        100   /* hold reset low before releasing */
#define NRF_RESET_HIGH_MS       50    /* wait after release before using */

/* UART inter-frame timing (µs) */
#define UART_WAKEUP_PULSE_US    50    /* wakeup pulse before UART transmit */
#define UART_TX_TIME_PER_BYTE   32    /* µs per byte at current baud */
#define UART_FRAME_GAP_US       200   /* gap between repeated frames */

/* UART BAT config delay (ms) */
#define UART_BATCFG_DELAY_MS    50

/* RF init retry loop (ms) */
#define RF_INIT_RETRY_DELAY_MS  5     /* wait between init retries */
#define RF_INIT_CMD_DELAY_MS    20    /* delayms passed to uart_send_cmd */

USART_MGR_STRUCT Usart_Mgr;
#define RX_SBYTE Usart_Mgr.RXDBuf[0]
#define RX_CMD Usart_Mgr.RXDBuf[1]
#define RX_ACK Usart_Mgr.RXDBuf[2]
#define RX_LEN Usart_Mgr.RXDBuf[3]
#define RX_DAT Usart_Mgr.RXDBuf[4]

uint8_t  uart_bit_report_buf[32] = {0};
uint8_t  func_tab[32]            = {0};
uint8_t  bitkb_report_buf[32]    = {0};
uint8_t  bytekb_report_buf[8]    = {0};
uint16_t conkb_report            = 0;
uint16_t syskb_report            = 0;
uint8_t  sync_lost               = 0;
uint8_t  disconnect_delay        = 0;
bool     uart_repeat_flag        = 0;

#define UART_DEFERRED_QUEUE_LEN 8

typedef struct {
    uint8_t  cmd;
    uint8_t  delayms;
    uint32_t timer;
} deferred_uart_cmd_t;

static deferred_uart_cmd_t deferred_uart_queue[UART_DEFERRED_QUEUE_LEN];
static uint8_t             deferred_uart_head  = 0;
static uint8_t             deferred_uart_tail  = 0;
static uint8_t             deferred_uart_count = 0;

static bool deferred_uart_cmd_pending(uint8_t cmd) {
    for (uint8_t i = 0; i < deferred_uart_count; i++) {
        // ponytail: The queue is fixed at 8 entries, so a linear scan is the
        // smallest reliable coalescing rule. If the queue grows meaningfully,
        // replace this with per-command pending flags.
        uint8_t index = (deferred_uart_head + i) % UART_DEFERRED_QUEUE_LEN;
        if (deferred_uart_queue[index].cmd == cmd) {
            return true;
        }
    }

    return false;
}

report_mouse_t mousekey_get_report(void);
void           uart_init(uint32_t baud); // qmk uart.c
void           UART_Send_Bytes(const uint8_t *Buffer, uint32_t Length);
uint8_t        get_checksum(const uint8_t *buf, uint8_t len);
uint16_t       host_last_consumer_usage(void);

/**
 * @brief Uart auto nkey send
 */
bool        f_bit_kb_act = 0;
static void uart_auto_nkey_send(const uint8_t *pre_bit_report, const uint8_t *now_bit_report, uint8_t size) {
    uint8_t i, j, byte_index;
    uint8_t change_mask, offset_mask;
    uint8_t key_code    = 0;
    bool    f_byte_send = 0, f_bit_send = 0;

    if (pre_bit_report[0] ^ now_bit_report[0]) {
        bytekb_report_buf[0] = now_bit_report[0];
        f_byte_send          = 1;
    }

    for (i = 1; i < size; i++) {
        change_mask = pre_bit_report[i] ^ now_bit_report[i];
        offset_mask = 1;
        for (j = 0; j < 8; j++) {
            if (change_mask & offset_mask) {
                if (now_bit_report[i] & offset_mask) {
                    for (byte_index = 2; byte_index < 8; byte_index++) {
                        if (bytekb_report_buf[byte_index] == 0) {
                            bytekb_report_buf[byte_index] = key_code;
                            f_byte_send                   = 1;
                            break;
                        }
                    }
                    if (byte_index >= 8) {
                        uart_bit_report_buf[i] |= offset_mask;
                        f_bit_send = 1;
                    }
                } else {
                    for (byte_index = 2; byte_index < 8; byte_index++) {
                        if (bytekb_report_buf[byte_index] == key_code) {
                            bytekb_report_buf[byte_index] = 0;
                            f_byte_send                   = 1;
                            break;
                        }
                    }
                    if (byte_index >= 8) {
                        uart_bit_report_buf[i] &= ~offset_mask;
                        f_bit_send = 1;
                    }
                }
            }
            key_code++;
            offset_mask <<= 1;
        }
    }

    if (f_bit_send) {
        f_bit_kb_act = 1;
        uart_send_report(CMD_RPT_BIT_KB, uart_bit_report_buf, 16);
    }

    if (f_byte_send) {
        uart_send_report(CMD_RPT_BYTE_KB, bytekb_report_buf, 8);
    }
}

/**
 * @brief  Uart send keys report.
 */
void uart_send_report_func(void) {
    static uint32_t interval_timer = 0;

    if (dev_info.link_mode == LINK_USB) return;

    if (timer_elapsed32(interval_timer) > RF_REPORT_INTERVAL_MS) {
        interval_timer = timer_read32();
        if (no_act_time <= RF_IDLE_THRESHOLD) {
            uart_send_report(CMD_RPT_BYTE_KB, bytekb_report_buf, 8);
            wait_us(UART_FRAME_GAP_US);

            if (f_bit_kb_act) uart_send_report(CMD_RPT_BIT_KB, uart_bit_report_buf, 16);
        } else {
            f_bit_kb_act = 0;
        }
    }
}

/**
 * @brief  Uart send consumer keys report.
 * @note Call in rf_driver.c
 */
void uart_send_consumer_report(report_extra_t *report) {
    no_act_time = 0;
    uart_send_report(CMD_RPT_CONSUME, (uint8_t *)(&report->usage), 2);
}

/**
 * @brief  Uart send mouse keys report.
 * @note Call in rf_driver.c
 */
void uart_send_mouse_report(report_mouse_t *report) {
    no_act_time = 0;
    uart_send_report(CMD_RPT_MS, &report->buttons, 5);
}

/**
 * @brief  Uart send system keys report.
 * @note Call in rf_driver.c
 */
void uart_send_system_report(report_extra_t *report) {
    no_act_time = 0;
    uart_send_report(CMD_RPT_SYS, (uint8_t *)(&report->usage), 2);
}

/**
 * @brief  Uart send byte keys report.
 * @note Call in rf_driver.c
 */
void uart_send_report_keyboard(report_keyboard_t *report) {
    no_act_time      = 0;
    report->reserved = 0;
    uart_send_report(CMD_RPT_BYTE_KB, &report->mods, 8);
    memcpy(bytekb_report_buf, &report->mods, 8);
}

/**
 * @brief  Uart send bit keys report.
 * @note Call in rf_driver.c
 */
void uart_send_report_nkro(report_nkro_t *report) {
    no_act_time = 0;
    uart_auto_nkey_send(bitkb_report_buf, &report->mods, NKRO_REPORT_BITS + 1);
    memcpy(&bitkb_report_buf[0], &report->mods, NKRO_REPORT_BITS + 1);
}

/**
 * @brief  Parsing the data received from the RF module.
 *
 * Frame formats from the RF module:
 *   3-byte bare ACK: [head][cmd][0xA0]
 *   5+ byte data:    [head][cmd][ack][len][data[len]][checksum]
 *
 * Validation happens in full before any ACK/sync flags are set, so a
 * malformed frame can never falsely satisfy a pending ack-wait in
 * uart_send_cmd() or suppress the sync-loss watchdog in dev_sts_sync().
 * Every exit path funnels through reset_rx so the parser is always left
 * in a clean RX_Idle state for the next frame.
 */
void RF_Protocol_Receive(void) {
    uint8_t i, check_sum = 0;

    if (Usart_Mgr.RXDState != RX_Done) return;

    /* --- 3-byte bare ACK: no payload, no command handler to run. --- */
    if (Usart_Mgr.RXDLen == 3) {
        if (Usart_Mgr.RXDBuf[2] != 0xA0) goto reset_rx;
        kbd_flags.uart_ack = 1;
        sync_lost  = 0;
        goto reset_rx;
    }

    /* --- Data frame: need at least head+cmd+ack+len+checksum (5 bytes). --- */
    if (Usart_Mgr.RXDLen < 5) goto reset_rx;

    /* Declared payload length must match what we actually received. */
    if ((Usart_Mgr.RXDLen - 5) != RX_LEN) goto reset_rx;

    /* Checksum covers the payload bytes only. */
    for (i = 0; i < RX_LEN; i++)
        check_sum += Usart_Mgr.RXDBuf[4 + i];
    if (check_sum != Usart_Mgr.RXDBuf[4 + i]) goto reset_rx;

    /*
     * Command-specific payload length guards.
     * Each handler reads fixed offsets; without these checks a short
     * but checksum-valid frame would read stale data left over in
     * RXDBuf from a previous (longer) frame.
     */
    switch (RX_CMD) {
        case CMD_RF_STS_SYSC:
            /* Handler reads bytes [4]-[8]: link, state, led, charge, battery. */
            if (RX_LEN < 5) goto reset_rx;
            break;
        case CMD_READ_DATA:
            /* Handler copies 32 bytes from [4] into func_tab. */
            if (RX_LEN < 32) goto reset_rx;
            break;
        default:
            break;
    }

    /* --- Frame fully validated: safe to commit ACK/sync state. --- */
    kbd_flags.uart_ack = 1;
    sync_lost  = 0;

    switch (RX_CMD) {
        case CMD_HAND: {
            kbd_flags.rf_hand_ok = 1;
            break;
        }

        case CMD_24G_SUSPEND: {
            kbd_flags.goto_sleep = 1;
            break;
        }

        case CMD_NEW_ADV: {
            kbd_flags.rf_new_adv_ok = 1;
            break;
        }

        case CMD_RF_STS_SYSC: {
            static uint8_t error_cnt = 0;

            if (dev_info.link_mode == Usart_Mgr.RXDBuf[4]) {
                error_cnt = 0;

                dev_info.rf_state = Usart_Mgr.RXDBuf[5];

                if ((dev_info.rf_state == RF_CONNECT) && ((Usart_Mgr.RXDBuf[6] & 0xf8) == 0)) {
                    dev_info.rf_led = Usart_Mgr.RXDBuf[6];
                }

                dev_info.rf_charge = Usart_Mgr.RXDBuf[7];

                // Trust the module's reported percentage even while charging.
                // The stock firmware pinned this to 100% whenever the charge
                // bit was set, which hid the real level on USB/charging.
                if (Usart_Mgr.RXDBuf[8] <= 100) dev_info.rf_baterry = Usart_Mgr.RXDBuf[8];
            } else {
                if (dev_info.rf_state != RF_INVALID) {
                    if (error_cnt >= 5) {
                        error_cnt      = 0;
                        kbd_flags.send_channel = 1;
                    } else {
                        error_cnt++;
                    }
                }
            }

            kbd_flags.rf_sts_sysc_ok = 1;
            break;
        }

        case CMD_READ_DATA: {
            memcpy(func_tab, &Usart_Mgr.RXDBuf[4], 32);

            if (func_tab[4] <= LINK_USB) {
                dev_info.link_mode = func_tab[4];
            }

            if (func_tab[5] < LINK_USB) {
                dev_info.rf_channel = func_tab[5];
            }

            if ((func_tab[6] <= LINK_BT_3) && (func_tab[6] >= LINK_BT_1)) {
                dev_info.ble_channel = func_tab[6];
            }

            kbd_flags.rf_read_data_ok = 1;
            break;
        }
    }

reset_rx:
    Usart_Mgr.RXDLen      = 0;
    Usart_Mgr.RXDState    = RX_Idle;
    Usart_Mgr.RXDOverTime = 0;
}

/**
 * @brief  Uart send cmd (fire-and-forget).
 * @param  cmd: cmd.
 * @param  wait_ack: unused — kept for call-site compatibility.
 * @param  delayms: delay before sending (blocking, boot-time only).
 *
 * The former ack-wait loop was a no-op: f_uart_ack is set by
 * RF_Protocol_Receive(), which is only called from uart_receive_pro(),
 * and neither is serviced inside this function.  No caller uses the
 * return value, so the wait was a pure blocking delay with no effect.
 */
uint8_t uart_send_cmd(uint8_t cmd, uint8_t wait_ack, uint8_t delayms) {
    (void)wait_ack;
    if (delayms) {
        wait_ms(delayms);
    }

    memset(&Usart_Mgr.TXDBuf[0], 0, UART_MAX_LEN);

    Usart_Mgr.TXDBuf[0] = UART_HEAD;
    Usart_Mgr.TXDBuf[1] = cmd;
    Usart_Mgr.TXDBuf[2] = 0x00;

    switch (cmd) {
        case CMD_SLEEP: {
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = 0;
            Usart_Mgr.TXDBuf[5] = 0;
            break;
        }

        case CMD_HAND: {
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = 0;
            Usart_Mgr.TXDBuf[5] = 0;
            break;
        }

        case CMD_RF_STS_SYSC: {
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = dev_info.link_mode;
            Usart_Mgr.TXDBuf[5] = dev_info.link_mode;
            break;
        }

        case CMD_SET_LINK: {
            dev_info.rf_state   = RF_LINKING;
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = dev_info.link_mode;
            Usart_Mgr.TXDBuf[5] = dev_info.link_mode;

            rf_linking_time  = 0;
            disconnect_delay = 0xff;
            break;
        }

        case CMD_NEW_ADV: {
            dev_info.rf_state   = RF_PAIRING;
            Usart_Mgr.TXDBuf[3] = 2;
            Usart_Mgr.TXDBuf[4] = dev_info.link_mode;
            Usart_Mgr.TXDBuf[5] = 1;
            Usart_Mgr.TXDBuf[6] = dev_info.link_mode + 1;

            rf_linking_time  = 0;
            disconnect_delay = 0xff;
            kbd_flags.rf_new_adv_ok  = 0;
            break;
        }

        case CMD_CLR_DEVICE: {
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = 0;
            Usart_Mgr.TXDBuf[5] = 0;
            break;
        }

        case CMD_SET_CONFIG: {
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = POWER_DOWN_DELAY;
            Usart_Mgr.TXDBuf[5] = POWER_DOWN_DELAY;
            break;
        }
        case CMD_SET_NAME: {
            Usart_Mgr.TXDBuf[3]  = 18;
            Usart_Mgr.TXDBuf[4]  = 1;
            Usart_Mgr.TXDBuf[5]  = 16;
            Usart_Mgr.TXDBuf[6]  = 'N';
            Usart_Mgr.TXDBuf[7]  = 'u';
            Usart_Mgr.TXDBuf[8]  = 'P';
            Usart_Mgr.TXDBuf[9]  = 'h';
            Usart_Mgr.TXDBuf[10] = 'y';
            Usart_Mgr.TXDBuf[11] = ' ';
            Usart_Mgr.TXDBuf[12] = 'H';
            Usart_Mgr.TXDBuf[13] = 'a';
            Usart_Mgr.TXDBuf[14] = 'l';
            Usart_Mgr.TXDBuf[15] = 'o';
            Usart_Mgr.TXDBuf[16] = '7';
            Usart_Mgr.TXDBuf[17] = '5';
            Usart_Mgr.TXDBuf[18] = ' ';
            Usart_Mgr.TXDBuf[19] = 'V';
            Usart_Mgr.TXDBuf[20] = '2';
            Usart_Mgr.TXDBuf[21] = '-';
            Usart_Mgr.TXDBuf[22] = get_checksum(Usart_Mgr.TXDBuf + 4, Usart_Mgr.TXDBuf[3]); // sum
            break;
        }

        case CMD_SET_24G_NAME: {
            Usart_Mgr.TXDBuf[3]  = 46;
            Usart_Mgr.TXDBuf[4]  = 46;
            Usart_Mgr.TXDBuf[5]  = 3;
            Usart_Mgr.TXDBuf[6]  = 'N';
            Usart_Mgr.TXDBuf[8]  = 'u';
            Usart_Mgr.TXDBuf[10] = 'P';
            Usart_Mgr.TXDBuf[12] = 'h';
            Usart_Mgr.TXDBuf[14] = 'y';
            Usart_Mgr.TXDBuf[16] = ' ';
            Usart_Mgr.TXDBuf[18] = 'H';
            Usart_Mgr.TXDBuf[20] = 'a';
            Usart_Mgr.TXDBuf[22] = 'l';
            Usart_Mgr.TXDBuf[24] = 'o';
            Usart_Mgr.TXDBuf[26] = '7';
            Usart_Mgr.TXDBuf[28] = '5';
            Usart_Mgr.TXDBuf[30] = ' ';
            Usart_Mgr.TXDBuf[32] = 'V';
            Usart_Mgr.TXDBuf[34] = '2';
            Usart_Mgr.TXDBuf[36] = ' ';
            Usart_Mgr.TXDBuf[38] = 'D';
            Usart_Mgr.TXDBuf[40] = 'o';
            Usart_Mgr.TXDBuf[42] = 'n';
            Usart_Mgr.TXDBuf[44] = 'g';
            Usart_Mgr.TXDBuf[46] = 'l';
            Usart_Mgr.TXDBuf[48] = 'e';
            Usart_Mgr.TXDBuf[50] = get_checksum(Usart_Mgr.TXDBuf + 4, Usart_Mgr.TXDBuf[3]); // sum
            break;
        }

        case CMD_READ_DATA: {
            Usart_Mgr.TXDBuf[3] = 2;
            Usart_Mgr.TXDBuf[4] = 0x00;
            Usart_Mgr.TXDBuf[5] = FUNC_VALID_LEN;
            Usart_Mgr.TXDBuf[6] = FUNC_VALID_LEN;
            break;
        }

        case CMD_RF_DFU: {
            Usart_Mgr.TXDBuf[3] = 1;
            Usart_Mgr.TXDBuf[4] = 0;
            Usart_Mgr.TXDBuf[5] = 0;
            break;
        }

        default:
            break;
    }

    kbd_flags.uart_ack = 0;
    UART_Send_Bytes(Usart_Mgr.TXDBuf, Usart_Mgr.TXDBuf[3] + 5);

    return TX_OK;
}

uint8_t uart_send_cmd_deferred(uint8_t cmd, uint8_t delayms) {
    if (cmd == CMD_RF_STS_SYSC) {
        if (deferred_uart_cmd_pending(cmd)) {
            return TX_OK;
        }

        if (deferred_uart_count >= UART_DEFERRED_QUEUE_LEN) {
            return TX_OK;
        }
    }

    if (deferred_uart_count >= UART_DEFERRED_QUEUE_LEN) {
        return TX_TIMEOUT;
    }

    deferred_uart_queue[deferred_uart_tail].cmd     = cmd;
    deferred_uart_queue[deferred_uart_tail].delayms = delayms;
    deferred_uart_queue[deferred_uart_tail].timer   = timer_read32();
    deferred_uart_tail                              = (deferred_uart_tail + 1) % UART_DEFERRED_QUEUE_LEN;
    deferred_uart_count++;

    return TX_OK;
}

void uart_send_cmd_deferred_task(void) {
    if (!deferred_uart_count) {
        return;
    }

    deferred_uart_cmd_t *cmd = &deferred_uart_queue[deferred_uart_head];
    if (timer_elapsed32(cmd->timer) < cmd->delayms) {
        return;
    }

    uart_send_cmd(cmd->cmd, 0, 0);
    deferred_uart_head = (deferred_uart_head + 1) % UART_DEFERRED_QUEUE_LEN;
    deferred_uart_count--;

    if (deferred_uart_count) {
        deferred_uart_queue[deferred_uart_head].timer = timer_read32();
    }
}

static bool rf_reset_task(void) {
    static uint8_t  reset_step  = 0;
    static uint32_t reset_timer = 0;

    if (kbd_flags.rf_reset && reset_step == 0) {
        kbd_flags.rf_reset  = 0;
        reset_step  = 1;
        reset_timer = timer_read32();
    }

    if (reset_step == 0) {
        return false;
    }

    if (reset_step == 1 && timer_elapsed32(reset_timer) >= NRF_RESET_LOW_MS) {
        gpio_write_pin_low(NRF_RESET_PIN);
        reset_step  = 2;
        reset_timer = timer_read32();
    } else if (reset_step == 2 && timer_elapsed32(reset_timer) >= NRF_RESET_HIGH_MS) {
        gpio_write_pin_high(NRF_RESET_PIN);
        reset_step  = 3;
        reset_timer = timer_read32();
    } else if (reset_step == 3 && timer_elapsed32(reset_timer) >= NRF_RESET_HIGH_MS) {
        reset_step = 0;
    }

    return reset_step != 0;
}

/**
 * @brief RF module state sync.
 */
void dev_sts_sync(void) {
    static uint32_t interval_timer  = 0;
    static uint8_t  link_state_temp = RF_DISCONNECT;

    if (rf_reset_task()) {
        return;
    }

    if (timer_elapsed32(interval_timer) < RF_SYNC_INTERVAL_MS)
        return;
    else
        interval_timer = timer_read32();

    if (kbd_flags.send_channel) {
        kbd_flags.send_channel = 0;
        uart_send_cmd_deferred(CMD_SET_LINK, 10);
    }

    if (dev_info.link_mode == LINK_USB) {
        if (host_mode != HOST_USB_TYPE) {
            host_mode = HOST_USB_TYPE;
            host_set_driver(m_host_driver);
            m_break_all_key();
        }
        rf_blink_cnt = 0;
    } else {
        if (host_mode != HOST_RF_TYPE) {
            host_mode = HOST_RF_TYPE;
            m_break_all_key();
            host_set_driver(&rf_host_driver);
        }

        if (dev_info.rf_state != RF_CONNECT) {
            if (disconnect_delay >= 10) {
                rf_blink_cnt      = 3;
                rf_link_show_time = 0;
                link_state_temp   = dev_info.rf_state;
            } else {
                disconnect_delay++;
            }
        } else if (dev_info.rf_state == RF_CONNECT) {
            rf_linking_time  = 0;
            disconnect_delay = 0;
            rf_blink_cnt     = 0;

            if (link_state_temp != RF_CONNECT) {
                link_state_temp   = RF_CONNECT;
                rf_link_show_time = 0;
                if (dev_info.link_mode == LINK_RF_24) {
                    uart_send_cmd_deferred(CMD_SET_24G_NAME, 30);
                }
            }
        }
    }

    uart_send_cmd_deferred(CMD_RF_STS_SYSC, 1);

    if (dev_info.link_mode != LINK_USB) {
        if (++sync_lost >= 5) {
            sync_lost  = 0;
            kbd_flags.rf_reset = 1;
        }
    }
}

#define BAT_CFG_LEN 80
const uint8_t battery_acfg_tab[BAT_CFG_LEN] = {
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4, 0xC2, 0xB4, 0xA8, 0x9B, 0x96, 0xF8, 0xF2, 0xF3, 0xC3, 0xA8, 0x8A, 0x65, 0x55, 0x49, 0x41, 0x39, 0x34, 0x2E, 0xA9, 0xAE, 0xD3, 0x28, 0xFF, 0xFF, 0xF1, 0xD3, 0xCE, 0xCB, 0xC8, 0xC3, 0xB8, 0xAE, 0xA7, 0xA8, 0xA6, 0x82, 0x6D, 0x65, 0x63, 0x69, 0x79, 0x8D, 0xA4, 0xB7, 0xC8, 0xA4, 0x16, 0x20, 0x00, 0xA7, 0x10, 0x00, 0xB1, 0x28, 0x00, 0x00, 0x00, 0x64, 0x43, 0xC0, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81,
};

void UART_Send_BatCfg(void) {
    uint8_t buf[128] = {0};
    _Static_assert(BAT_CFG_LEN + 5 <= sizeof(buf), "battery config exceeds UART buffer");

    buf[0] = UART_HEAD;
    buf[1] = CMD_WBAT_CFG;
    buf[2] = 0x01;
    buf[3] = BAT_CFG_LEN;
    memcpy(&buf[4], battery_acfg_tab, BAT_CFG_LEN);
    buf[4 + BAT_CFG_LEN] = get_checksum(&buf[4], BAT_CFG_LEN);
    UART_Send_Bytes(buf, BAT_CFG_LEN + 5);
    wait_ms(UART_BATCFG_DELAY_MS);
}

/**
 * @brief Uart send bytes.
 * @param Buffer data buf
 * @param Length data length
 */
void UART_Send_Bytes(const uint8_t *Buffer, uint32_t Length) {
    if (uart_repeat_flag) {
        for (uint8_t i = 0; i < 3; i++) {
            gpio_write_pin_low(NRF_WAKEUP_PIN);
            wait_us(UART_WAKEUP_PULSE_US);

            uart_transmit(Buffer, Length);

            wait_us(UART_WAKEUP_PULSE_US + Length * UART_TX_TIME_PER_BYTE);
            gpio_write_pin_high(NRF_WAKEUP_PIN);

            wait_us(UART_FRAME_GAP_US);
        }
    } else {
        gpio_write_pin_low(NRF_WAKEUP_PIN);
        wait_us(UART_WAKEUP_PULSE_US);

        uart_transmit(Buffer, Length);

        wait_us(UART_WAKEUP_PULSE_US + Length * UART_TX_TIME_PER_BYTE);
        gpio_write_pin_high(NRF_WAKEUP_PIN);
    }
}

/**
 * @brief get checksum.
 * @param buf data buf
 * @param len data length
 */
uint8_t get_checksum(const uint8_t *buf, uint8_t len) {
    uint8_t i;
    uint8_t checksum = 0;

    for (i = 0; i < len; i++)
        checksum += *buf++;

    checksum ^= UART_HEAD;

    return checksum;
}

/**
 * @brief Uart send report.
 * @param report_type  report_type
 * @param report_buf  report_buf
 * @param report_size  report_size
 */
void uart_send_report(uint8_t report_type, const uint8_t *report_buf, uint8_t report_size) {
    if (kbd_flags.dial_sw_init_ok == 0) return;
    if (dev_info.link_mode == LINK_USB) return;
    if (dev_info.rf_state != RF_CONNECT) return;

    /* TXDBuf is UART_MAX_LEN (64) bytes: 4-byte header + payload + 1-byte checksum. */
    if (report_size + 5 > UART_MAX_LEN) return;

    Usart_Mgr.TXDBuf[0] = UART_HEAD;
    Usart_Mgr.TXDBuf[1] = report_type;
    Usart_Mgr.TXDBuf[2] = 0x01;
    Usart_Mgr.TXDBuf[3] = report_size;

    memcpy(&Usart_Mgr.TXDBuf[4], report_buf, report_size);
    Usart_Mgr.TXDBuf[4 + report_size] = get_checksum(&Usart_Mgr.TXDBuf[4], report_size);

    uart_repeat_flag = 1;

    UART_Send_Bytes(&Usart_Mgr.TXDBuf[0], report_size + 5);

    uart_repeat_flag = 0;

    wait_us(UART_FRAME_GAP_US);
}

/**
 * @brief Uart receives data and processes it after completion,.
 */
void uart_receive_pro(void) {
    static bool     rcv_start    = false;
    static uint16_t last_rx_time = 0;

    /* Receiving serial data from RF module.
     * Drain the hardware FIFO without blocking; a 200µs inter-byte gap
     * (checked non-blockingly below) signals end-of-frame. */
    while (uart_available()) {
        rcv_start    = true;
        last_rx_time = timer_read();

        if (Usart_Mgr.RXDLen >= UART_MAX_LEN) {
            uart_read();
            Usart_Mgr.RXDLen   = 0;
            Usart_Mgr.RXDState = RX_Idle;
        } else {
            Usart_Mgr.RXDBuf[Usart_Mgr.RXDLen++] = uart_read();
        }
    }

    /* Process the frame only after 200µs with no new data — confirms
     * the frame is complete. Non-blocking: if the gap hasn't elapsed
     * yet, we return and retry on the next housekeeping tick. */
    if (rcv_start && timer_elapsed(last_rx_time) >= 200) {
        rcv_start          = false;
        Usart_Mgr.RXDState = RX_Done;
        RF_Protocol_Receive();
        Usart_Mgr.RXDLen = 0;
    }
}

/**
 * @brief  RF uart initial.
 */
void rf_uart_init(void) {
    /* set uart buad as 460800 */
    uart_init(460800);

    /* Enable parity check */
    USART1->CR1 &= ~((uint32_t)USART_CR1_UE);
    USART1->CR1 |= USART_CR1_M0 | USART_CR1_PCE;
    USART1->CR1 |= USART_CR1_UE;

    /* set Rx and Tx pin pull up */
    GPIOB->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR6 | GPIO_OSPEEDER_OSPEEDR7);
    GPIOB->PUPDR |= (GPIO_PUPDR_PUPDR6_0 | GPIO_PUPDR_PUPDR7_0);
}

/**
 * @brief RF module initial.
 */
void rf_device_init(void) {
    uint8_t timeout = 0;

    timeout      = 10;
    kbd_flags.rf_hand_ok = 0;
    while (timeout--) {
        uart_send_cmd(CMD_HAND, 0, RF_INIT_CMD_DELAY_MS);
        wait_ms(RF_INIT_RETRY_DELAY_MS);
        uart_receive_pro(); // receive data
        uart_receive_pro(); // parsing data
        if (kbd_flags.rf_hand_ok) break;
    }

    timeout           = 10;
    kbd_flags.rf_read_data_ok = 0;
    while (timeout--) {
        uart_send_cmd(CMD_READ_DATA, 0, RF_INIT_CMD_DELAY_MS);
        wait_ms(RF_INIT_RETRY_DELAY_MS);
        uart_receive_pro();
        uart_receive_pro();
        if (kbd_flags.rf_read_data_ok) break;
    }

    timeout          = 10;
    kbd_flags.rf_sts_sysc_ok = 0;
    while (timeout--) {
        uart_send_cmd(CMD_RF_STS_SYSC, 0, RF_INIT_CMD_DELAY_MS);
        wait_ms(RF_INIT_RETRY_DELAY_MS);
        uart_receive_pro();
        uart_receive_pro();
        if (kbd_flags.rf_sts_sysc_ok) break;
    }

    UART_Send_BatCfg();

    uart_send_cmd(CMD_SET_NAME, 10, RF_INIT_CMD_DELAY_MS);

    uart_send_cmd(CMD_SET_24G_NAME, 10, RF_INIT_CMD_DELAY_MS);
}
