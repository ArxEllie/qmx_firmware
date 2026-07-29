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
#include "usb_device_state.h"
#include "mcu_pwr.h"
#include "keycode_config.h"

#define USB_WAKE_EVENT_QUEUE_LEN 32
#define RF_SLEEP_COMMAND_SETTLE_MS 5
#define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)

typedef struct {
    keypos_t key;
    uint32_t scan_id;
    bool     pressed;
    bool     modifier_press;
} usb_wake_event_t;

static usb_wake_event_t usb_wake_events[USB_WAKE_EVENT_QUEUE_LEN];
static uint8_t          usb_wake_event_head;
static uint8_t          usb_wake_event_count;
static uint32_t         usb_wake_scan_id;
static bool             usb_wake_requested;
static bool             usb_resume_restore_pending;
static virtual_timer_t  usb_remote_wake_timer;
static bool             usb_remote_wake_timer_initialized;
static uint8_t          usb_suspend_debounce;

#ifdef CONSOLE_ENABLE
static uint16_t debug_usb_wake_cycles;
static uint16_t debug_usb_wake_queued;
static uint16_t debug_usb_wake_replayed;
static uint16_t debug_usb_wake_restored;
static uint16_t debug_usb_wake_overflows;

static void debug_counter_increment(uint16_t *counter) {
    if (*counter < UINT16_MAX) {
        (*counter)++;
    }
}

/* Snapshot and reset from the low-frequency keyboard reporter. No console
 * output occurs in the wake/event path, where printing would distort timing. */
void usb_wakeup_debug_take(uint16_t *cycles, uint16_t *queued, uint16_t *replayed, uint16_t *restored, uint16_t *overflows) {
    *cycles    = debug_usb_wake_cycles;
    *queued    = debug_usb_wake_queued;
    *replayed  = debug_usb_wake_replayed;
    *restored  = debug_usb_wake_restored;
    *overflows = debug_usb_wake_overflows;

    debug_usb_wake_cycles    = 0;
    debug_usb_wake_queued    = 0;
    debug_usb_wake_replayed  = 0;
    debug_usb_wake_restored  = 0;
    debug_usb_wake_overflows = 0;
}
#endif

_Static_assert(USB_WAKE_EVENT_QUEUE_LEN <= UINT8_MAX, "USB wake queue indices must fit in uint8_t");

/* QMK owns this snapshot in keyboard.c. suspend_wakeup_init() deliberately
 * leaves it intact while clearing logical reports, which is exactly the
 * distinction needed to identify a key held continuously across cleanup. */
extern matrix_row_t matrix_previous[MATRIX_ROWS];

/* ChibiOS changes USB_DRIVER.state from SUSPENDED to ACTIVE in interrupt
 * context, then defers QMK's USB_EVENT_WAKEUP handler to protocol_pre_task().
 * That handler clears the keyboard state before marking the logical USB state
 * CONFIGURED again. Both states must therefore be ready before a physical
 * event can safely reach QMK or a queued wake event can be replayed. */
static bool usb_hid_resume_cleanup_complete(void) {
    return USB_DRIVER.state == USB_ACTIVE && usb_device_state_get_configure_state() == USB_DEVICE_STATE_CONFIGURED;
}

/* ChibiOS' usb_lld_wakeup_host() holds L2RES with a blocking 2 ms thread
 * sleep. Wake keys run in the matrix event path, so use the same STM32 pulse
 * asynchronously and let a kernel virtual timer end it at the required time.
 * This callback runs in interrupt context and only performs one register
 * operation; all queue and USB state ownership remains in the main thread. */
static void usb_remote_wakeup_pulse_end(virtual_timer_t *timer, void *arg) {
    (void)timer;
    (void)arg;

    /* The USB ISR also updates CNTR. Keep this read-modify-write atomic so
     * ending the pulse cannot restore a stale copy of another USB flag. */
    chSysLockFromISR();
    STM32_USB->CNTR &= ~USB_CNTR_L2RES;
    chSysUnlockFromISR();
}

static void usb_remote_wakeup_request(void) {
    if (dev_info.link_mode != LINK_USB || USB_DRIVER.state != USB_SUSPENDED || usb_wake_requested || !(USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED)) {
        return;
    }

    if (!usb_remote_wake_timer_initialized) {
        chVTObjectInit(&usb_remote_wake_timer);
        usb_remote_wake_timer_initialized = true;
    }

    usb_wake_requested = true;

    /* Arm the pulse and its end timer atomically with respect to USB IRQs.
     * chVTSetI is the non-locking timer primitive for this locked section. */
    chSysLock();
    STM32_USB->CNTR |= USB_CNTR_L2RES;
    chVTSetI(&usb_remote_wake_timer, TIME_MS2I(STM32_USB_HOST_WAKEUP_DURATION), usb_remote_wakeup_pulse_end, NULL);
    chSysUnlock();
}

static bool matrix_has_pressed_key(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        if (matrix_get_row(row) != 0) {
            return true;
        }
    }

    return false;
}

static uint8_t physical_key_restore_pass(uint8_t row, uint8_t col) {
    keypos_t key     = {.row = row, .col = col};
    uint16_t keycode = keycode_config(keymap_key_to_keycode(layer_switch_get_layer(key), key));

    if (IS_MODIFIER_KEYCODE(keycode)) return 1;
    if (IS_BASIC_KEYCODE(keycode)) return 2;
    return 0;
}

void usb_wakeup_note_resume_cleanup(void) {
    if (dev_info.link_mode == LINK_USB) {
        usb_resume_restore_pending = true;
    }
}

/* suspend_wakeup_init() clears QMK's action state but does not alter the
 * debounced matrix or matrix_previous. A key held continuously across that
 * cleanup therefore has no new edge to recreate its action. Restore only
 * basic/modifier positions that were also present in matrix_previous and are
 * absent from the wake queue. The previous-state intersection excludes a new
 * edge in the first post-resume scan, which matrix_task() must dispatch
 * normally. Queued positions are reconstructed by their original transitions
 * below. Layer, macro, and other stateful keycodes are deliberately excluded
 * because wake cleanup does not provide a safe generic reconstruction. */
static void usb_wakeup_restore_unchanged_holds(const matrix_row_t queued_positions[MATRIX_ROWS]) {
    for (uint8_t restore_pass = 1; restore_pass <= 2; restore_pass++) {
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            matrix_row_t held_without_transition = matrix_get_row(row) & matrix_previous[row] & ~queued_positions[row];

            for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                matrix_row_t mask = MATRIX_ROW_SHIFTER << col;
                if (!(held_without_transition & mask) || physical_key_restore_pass(row, col) != restore_pass) continue;

                action_exec(MAKE_KEYEVENT(row, col, true));
#ifdef CONSOLE_ENABLE
                debug_counter_increment(&debug_usb_wake_restored);
#endif
            }
        }
    }
}

/**
 * @brief  Wake up from light sleep — called by pre_process_record_kb
 *         on the first keypress for instant wakeup (no 50ms poll delay).
 */
void wakeup_handle(void) {
    /* A key may arrive one matrix pass after Sleep_Handle() scheduled sleep
     * but before the next housekeeping pass consumes that request. Cancel it
     * here, at the event boundary, so the wake key is never followed by a
     * stale report clear and another immediate suspend attempt. */
    kbd_flags.goto_sleep = 0;
    usb_suspend_debounce = 0;

    if (kbd_flags.wakeup_prepare) {
        kbd_flags.wakeup_prepare = 0;
        no_act_time              = 0;

        /* Restore local hardware state first, then request one non-blocking
         * USB resume pulse. Additional events are held in the wake queue. */
        exit_light_sleep();
        usb_remote_wakeup_request();
        return;
    }

    /* USB can suspend before Sleep_Handle's 1 s debounce has entered
     * light sleep. A key must still request remote wake in that window. */
    usb_remote_wakeup_request();
}

/**
 * @brief Hold physical key events while the selected USB host is suspended.
 *
 * NO_USB_STARTUP_CHECK is required for wireless mode, so QMK's normal
 * suspend loop cannot guard keyboard_task(). Without this local guard, a
 * wake key is processed immediately and then erased by ChibiOS'
 * suspend_wakeup_init() when the USB wake event arrives. Replaying the
 * physical events after USB_ACTIVE prevents ChibiOS from erasing them.
 *
 * QMK dispatches simultaneous matrix changes in row order. On this keyboard
 * the letter rows precede the bottom modifier row, so a wake scan containing
 * Cmd+C naturally arrives here as C then Cmd. Events retain a scan ID so the
 * replay task can put modifier presses first within that one scan without
 * reordering keys typed during different scans.
 *
 * @return true when the caller must swallow the event until replay.
 */
bool usb_wakeup_defer_record(uint16_t keycode, keyrecord_t *record) {
    if (dev_info.link_mode != LINK_USB) {
        return false;
    }

    if (usb_hid_resume_cleanup_complete()) {
        return false;
    }

#ifdef CONSOLE_ENABLE
    if (usb_wake_event_count == 0) {
        debug_counter_increment(&debug_usb_wake_cycles);
    }
#endif

    if (usb_wake_event_count < USB_WAKE_EVENT_QUEUE_LEN) {
        uint8_t tail                         = (usb_wake_event_head + usb_wake_event_count) % USB_WAKE_EVENT_QUEUE_LEN;
        usb_wake_events[tail].key            = record->event.key;
        usb_wake_events[tail].scan_id        = usb_wake_scan_id;
        usb_wake_events[tail].pressed        = record->event.pressed;
        usb_wake_events[tail].modifier_press = record->event.pressed && IS_MODIFIER_KEYCODE(keycode_config(keycode));
        usb_wake_event_count++;
#ifdef CONSOLE_ENABLE
        debug_counter_increment(&debug_usb_wake_queued);
    } else {
        debug_counter_increment(&debug_usb_wake_overflows);
#endif
    }

    /* A human cannot fill 32 transitions during a normal USB resume.
     * If hardware chatter does fill it, keep swallowing events rather
     * than leaking a partial shortcut into a suspended endpoint. */
    wakeup_handle();
    return true;
}

/**
 * @brief Replay wake-time key events once ChibiOS has resumed USB endpoints.
 */
void usb_wakeup_replay_task(void) {
    /* This hook runs once after every physical matrix scan and before QMK
     * dispatches that scan's changes. Events queued later in the loop therefore
     * share this ID; wraparound is harmless because queued groups stay adjacent. */
    usb_wake_scan_id++;

    if (usb_wake_event_count == 0 && !usb_resume_restore_pending) return;

    if (dev_info.link_mode != LINK_USB) {
        usb_wake_event_head        = 0;
        usb_wake_event_count       = 0;
        usb_wake_requested         = false;
        usb_resume_restore_pending = false;
        return;
    }

    if (!usb_hid_resume_cleanup_complete()) return;

    matrix_row_t queued_positions[MATRIX_ROWS] = {0};
    for (uint8_t offset = 0; offset < usb_wake_event_count; offset++) {
        uint8_t                 index        = (usb_wake_event_head + offset) % USB_WAKE_EVENT_QUEUE_LEN;
        const usb_wake_event_t *queued_event = &usb_wake_events[index];
        queued_positions[queued_event->key.row] |= MATRIX_ROW_SHIFTER << queued_event->key.col;
    }
    usb_wakeup_restore_unchanged_holds(queued_positions);
    usb_resume_restore_pending = false;

    while (usb_wake_event_count > 0) {
        uint32_t scan_id     = usb_wake_events[usb_wake_event_head].scan_id;
        uint8_t  group_count = 0;

        while (group_count < usb_wake_event_count) {
            uint8_t index = (usb_wake_event_head + group_count) % USB_WAKE_EVENT_QUEUE_LEN;
            if (usb_wake_events[index].scan_id != scan_id) break;
            group_count++;
        }

        /* ponytail: two passes over at most 32 queued events are cheaper and
         * safer than mutating the ring buffer. Increase the queue only if USB
         * resume is ever expected to retain more than a human-sized burst. */
        for (uint8_t modifiers_first = 1; modifiers_first <= 2; modifiers_first++) {
            bool replay_modifiers = modifiers_first == 1;
            for (uint8_t offset = 0; offset < group_count; offset++) {
                uint8_t                 index        = (usb_wake_event_head + offset) % USB_WAKE_EVENT_QUEUE_LEN;
                const usb_wake_event_t *queued_event = &usb_wake_events[index];

                if (queued_event->modifier_press == replay_modifiers) {
                    action_exec(MAKE_KEYEVENT(queued_event->key.row, queued_event->key.col, queued_event->pressed));
#ifdef CONSOLE_ENABLE
                    debug_counter_increment(&debug_usb_wake_replayed);
#endif
                }
            }
        }

        usb_wake_event_head = (usb_wake_event_head + group_count) % USB_WAKE_EVENT_QUEUE_LEN;
        usb_wake_event_count -= group_count;
    }

    usb_wake_event_head = 0;
    usb_wake_requested  = false;
}

/**
 * @brief  Sleep Handle — runs every 50ms from housekeeping_task_kb.
 *
 * Three independent toggles control sleep behaviour:
 *   f_dev_sleep_enable  — master switch (SLEEP_MODE keycode)
 *   f_usb_sleep_enable  — allow light sleep on USB suspend / inactivity
 *   f_deep_sleep_enable — allow deep sleep (STOP mode) on RF+battery
 *
 * sleep_timeout (1–60 min) is configurable via ee_sleep_timeout.
 */
bool Sleep_Handle(void) {
    static uint32_t delay_step_timer     = 0;
    static uint32_t rf_disconnect_time   = 0;
    static bool     deep_sleep_preparing = false;
    static uint32_t deep_sleep_timer     = 0;
    static uint32_t deep_sleep_no_act    = 0;

    /* RF sleep commands receive acknowledgements asynchronously. Keep the
     * main loop alive long enough to drain them, but suppress new RF traffic.
     * A falling activity counter means a key event occurred and cancels the
     * transition before the MCU or radio can go to sleep. */
    if (deep_sleep_preparing) {
        bool sleep_cancelled = matrix_has_pressed_key() || dev_info.link_mode == LINK_USB || (dev_info.rf_charge & 0x01) != 0 || !f_dev_sleep_enable || !f_deep_sleep_enable || no_act_time < deep_sleep_no_act;

        if (sleep_cancelled) {
            deep_sleep_preparing = false;
            uart_send_cmd_deferred(CMD_HAND, 1);
            return false;
        }

        if (timer_elapsed32(deep_sleep_timer) < RF_SLEEP_COMMAND_SETTLE_MS) {
            return true;
        }

        deep_sleep_preparing = false;
        enter_deep_sleep();
        exit_deep_sleep();
        no_act_time = 0;
        return false;
    }

    /* 50ms interval */
    if (timer_elapsed32(delay_step_timer) < 50) return false;
    delay_step_timer = timer_read32();

    /* Master sleep toggle off — nothing to do. */
    if (!f_dev_sleep_enable) return false;

    uint32_t sleep_ticks = SLEEP_TIMEOUT_TO_TICKS(user_config.ee_sleep_timeout);

    /* sleep process */
    if (kbd_flags.goto_sleep) {
        /* Never clear reports or change power state while a key is held.
         * Some sleep requests originate in the RF module rather than a local
         * key event, so wakeup_handle() cannot be the only cancellation path.
         * Once clear_keyboard() erases a held key, QMK has no new press edge
         * with which to restore it before the user releases and presses again. */
        if (matrix_has_pressed_key()) {
            kbd_flags.goto_sleep = 0;
            usb_suspend_debounce = 0;
            return false;
        }

        kbd_flags.goto_sleep = 0;
        usb_suspend_debounce = 0;
        rf_disconnect_time   = 0;
        rf_linking_time      = 0;

        if (dev_info.link_mode == LINK_USB) {
            /* USB: light sleep only if usb_sleep is enabled or host
             * actually suspended the bus. */
            if (f_usb_sleep_enable || USB_DRIVER.state == USB_SUSPENDED) {
                m_break_all_key();
                enter_light_sleep();
                kbd_flags.wakeup_prepare = 1;
            }
        } else if ((dev_info.rf_charge & 0x01) != 0 || dev_info.rf_charge == 0x03) {
            /* RF + charging: light sleep (MCU must supervise charging). */
            m_break_all_key();
            enter_light_sleep();
            kbd_flags.wakeup_prepare = 1;
        } else {
            /* RF + battery: deep sleep if enabled, else light sleep. */
            m_break_all_key();
            if (f_deep_sleep_enable) {
                /* Phase one: put the nRF module to sleep while the MCU keeps
                 * scanning and drains the command acknowledgement. */
                prepare_deep_sleep();
                deep_sleep_preparing = true;
                deep_sleep_timer     = timer_read32();
                deep_sleep_no_act    = no_act_time;
                return true;
            } else {
                enter_light_sleep();
                kbd_flags.wakeup_prepare = 1;
            }
        }
    }

    /* sleep check */
    if (kbd_flags.goto_sleep || kbd_flags.wakeup_prepare) return false;

    if (dev_info.link_mode == LINK_USB) {
        if (USB_DRIVER.state == USB_SUSPENDED) {
            usb_suspend_debounce++;
            if (usb_suspend_debounce >= 20) {
                kbd_flags.goto_sleep = 1;
            }
        } else {
            usb_suspend_debounce = 0;
            if (f_usb_sleep_enable && no_act_time >= sleep_ticks) {
                kbd_flags.goto_sleep = 1;
            }
        }
    } else if (dev_info.rf_state == RF_CONNECT) {
        rf_disconnect_time = 0;
        if (no_act_time >= sleep_ticks) {
            kbd_flags.goto_sleep = 1;
        }
    } else if (rf_linking_time >= LINK_TIMEOUT) {
        rf_linking_time      = 0;
        kbd_flags.goto_sleep = 1;
    } else if (dev_info.rf_state == RF_DISCONNECT) {
        rf_disconnect_time++;
        if (rf_disconnect_time > 5 * 20) {
            rf_disconnect_time   = 0;
            kbd_flags.goto_sleep = 1;
        }
    }

    return false;
}
