// Asymmetric per-key debounce with separate press/release times.
// Based on QMK's built-in asym_eager_defer_pk.c, modified to read
// configurable press/release debounce intervals from user_config
// (stored in EEPROM, adjustable via VIA keycodes).
//
// Press:  eager — state changes immediately, then locks out for
//         debounce_press_ms before accepting another transition.
// Release: deferred — state changes only after no raw changes for
//          debounce_release_ms.

#include "debounce.h"
#include "timer.h"
#include "util.h"
#include "halo75_v2.h"

#ifndef DEBOUNCE
#    define DEBOUNCE 5
#endif

#define DEBOUNCE_ELAPSED 0

#if DEBOUNCE > 0
typedef struct {
    bool    pressed : 1;
    uint8_t time : 7;
} debounce_counter_t;

static debounce_counter_t debounce_counters[MATRIX_ROWS * MATRIX_COLS] = {DEBOUNCE_ELAPSED};
static bool               counters_need_update;
static bool               matrix_need_update;
static bool               cooked_changed;

static inline void update_debounce_counters_and_transfer_if_expired(matrix_row_t raw[], matrix_row_t cooked[], uint8_t elapsed_time);
static inline void transfer_matrix_values(matrix_row_t raw[], matrix_row_t cooked[]);

void debounce_init(void) {}

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    static fast_timer_t last_time;
    bool                updated_last = false;
    cooked_changed                   = false;

    if (counters_need_update) {
        fast_timer_t now          = timer_read_fast();
        fast_timer_t elapsed_time = TIMER_DIFF_FAST(now, last_time);

        last_time    = now;
        updated_last = true;

        if (elapsed_time > 0) {
            update_debounce_counters_and_transfer_if_expired(raw, cooked, MIN(elapsed_time, 127));
        }
    }

    if (changed || matrix_need_update) {
        if (!updated_last) {
            last_time = timer_read_fast();
        }

        transfer_matrix_values(raw, cooked);
    }

    return cooked_changed;
}

static inline void update_debounce_counters_and_transfer_if_expired(matrix_row_t raw[], matrix_row_t cooked[], uint8_t elapsed_time) {
    counters_need_update = false;
    matrix_need_update   = false;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        uint16_t row_offset = row * MATRIX_COLS;

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t index = row_offset + col;

            if (debounce_counters[index].time != DEBOUNCE_ELAPSED) {
                if (debounce_counters[index].time <= elapsed_time) {
                    debounce_counters[index].time = DEBOUNCE_ELAPSED;

                    if (debounce_counters[index].pressed) {
                        // key-down: eager — re-enable scanning for this key
                        matrix_need_update = true;
                    } else {
                        // key-up: defer — commit the release now
                        matrix_row_t col_mask    = (MATRIX_ROW_SHIFTER << col);
                        matrix_row_t cooked_next = (cooked[row] & ~col_mask) | (raw[row] & col_mask);
                        cooked_changed |= cooked_next ^ cooked[row];
                        cooked[row] = cooked_next;
                    }
                } else {
                    debounce_counters[index].time -= elapsed_time;
                    counters_need_update = true;
                }
            }
        }
    }
}

static inline void transfer_matrix_values(matrix_row_t raw[], matrix_row_t cooked[]) {
    matrix_need_update = false;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        uint16_t     row_offset = row * MATRIX_COLS;
        matrix_row_t delta      = raw[row] ^ cooked[row];

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t     index    = row_offset + col;
            matrix_row_t col_mask = (MATRIX_ROW_SHIFTER << col);

            if (delta & col_mask) {
                if (debounce_counters[index].time == DEBOUNCE_ELAPSED) {
                    debounce_counters[index].pressed = (raw[row] & col_mask);
                    counters_need_update             = true;

                    if (debounce_counters[index].pressed) {
                        // key-down: eager — commit immediately
                        cooked[row] ^= col_mask;
                        cooked_changed                = true;
                        debounce_counters[index].time = MAX(1, user_config.ee_debounce_press_ms);
                    } else {
                        // key-up: defer — wait release_ms before committing
                        debounce_counters[index].time = MAX(1, user_config.ee_debounce_release_ms);
                    }
                }
            } else if (debounce_counters[index].time != DEBOUNCE_ELAPSED) {
                if (!debounce_counters[index].pressed) {
                    // key-up: defer — raw bounced back, cancel pending release
                    debounce_counters[index].time = DEBOUNCE_ELAPSED;
                }
            }
        }
    }
}

#else
#    include "none.c"
#endif
