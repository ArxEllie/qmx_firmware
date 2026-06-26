/*
Copyright 2024 SHVD3x

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

#include <stdint.h>
#include <stdbool.h>
#include "wait.h"
#include "util.h"
#include "matrix.h"
#include "debounce.h"
#include "quantum.h"

/* Port bit masks for row and column pins.
 * Rows: C14, C15, A0, A1, A2, A3
 * Cols: A4, A5, A6, A7, A8, A9, A10, A15, B0, B1, B3, B10-B15 */
#define rowA_bits (PAL_PORT_BIT(PAL_PAD(A0)) | PAL_PORT_BIT(PAL_PAD(A1)) | PAL_PORT_BIT(PAL_PAD(A2)) | PAL_PORT_BIT(PAL_PAD(A3)))
#define rowC_bits (PAL_PORT_BIT(PAL_PAD(C14)) | PAL_PORT_BIT(PAL_PAD(C15)))
#define colA_bits (PAL_PORT_BIT(PAL_PAD(A4)) | PAL_PORT_BIT(PAL_PAD(A5)) | PAL_PORT_BIT(PAL_PAD(A6)) | PAL_PORT_BIT(PAL_PAD(A7)) | PAL_PORT_BIT(PAL_PAD(A8)) | PAL_PORT_BIT(PAL_PAD(A9)) | PAL_PORT_BIT(PAL_PAD(A10)) | PAL_PORT_BIT(PAL_PAD(A15)))
#define colB_bits (PAL_PORT_BIT(PAL_PAD(B0)) | PAL_PORT_BIT(PAL_PAD(B1)) | PAL_PORT_BIT(PAL_PAD(B3)) | PAL_PORT_BIT(PAL_PAD(B10)) | PAL_PORT_BIT(PAL_PAD(B11)) | PAL_PORT_BIT(PAL_PAD(B12)) | PAL_PORT_BIT(PAL_PAD(B13)) | PAL_PORT_BIT(PAL_PAD(B14)) | PAL_PORT_BIT(PAL_PAD(B15)))

#ifndef MATRIX_DEBOUNCE
#    define MATRIX_DEBOUNCE 10
#endif

/* Max settle-loop iterations per row before giving up.  Each iteration is
 * ~2 port reads + a branch (~12 cycles on Cortex-M0 @ 48 MHz ≈ 0.25 µs).
 * Worst case: 6 rows × 1000 iters × 0.25 µs ≈ 1.5 ms — a hard ceiling on
 * how long a single matrix_scan_custom() call can block. */
#define MATRIX_SETTLE_MAX_ITERS 1000

/* matrix state(1:on, 0:off) */
extern matrix_row_t raw_matrix[MATRIX_ROWS]; // raw values
extern matrix_row_t matrix[MATRIX_ROWS];     // debounced values

/* Ultra-fast column read: read both GPIO ports in two instructions,
 * then extract all 17 column bits with bit manipulation.
 *
 * COL2ROW: rows are driven LOW, columns are pulled HIGH by pull-up.
 * Key pressed → diode conducts, pulls column LOW.
 * Bit is set (1) when key is pressed, so we XOR with the idle-HIGH mask. */
static inline matrix_row_t read_cols(void) {
    uint16_t portA_pin_state = palReadPort(PAL_PORT(A0));
    uint16_t portB_pin_state = palReadPort(PAL_PORT(B0));
    return ((((portA_pin_state & 0b11110000) ^ 0b11110000) >> 4) |                 /* A4-A7  → bits 0-3  */
            (((portB_pin_state & 0b11) ^ 0b11) << 4) |                             /* B0,B1  → bits 4-5  */
            (((portB_pin_state & 0b1111110000000000) ^ 0b1111110000000000) >> 4) | /* B10-B15 → bits 6-11 */
            (((portA_pin_state & 0b11100000000) ^ 0b11100000000) << 4) |           /* A8-A10 → bits 12-14 */
            ((portA_pin_state & 0b1000000000000000) ^ 0b1000000000000000) |        /* A15 → bit 15 */
            (((portB_pin_state & 0b1000) ^ 0b1000) << 13));                        /* B3     → bit 16 */
}

static inline void unselect_rows(void) {
    palSetPort(PAL_PORT(A0), rowA_bits);
    palSetPort(PAL_PORT(C0), rowC_bits);
}

static inline void select_row(uint8_t row) {
    if (row > 1)
        palClearPort(PAL_PORT(A0), PAL_PORT_BIT(row - 2));
    else
        palClearPort(PAL_PORT(C0), PAL_PORT_BIT(row + 14));
}

void matrix_init_custom(void) {
    /* Rows as output push-pull, default HIGH (unselected). */
    palSetGroupMode(PAL_PORT(A0), rowA_bits, 0U, (PAL_STM32_MODE_OUTPUT | PAL_STM32_OTYPE_PUSHPULL | PAL_STM32_OSPEED_LOWEST));
    palSetGroupMode(PAL_PORT(C0), rowC_bits, 0U, (PAL_STM32_MODE_OUTPUT | PAL_STM32_OTYPE_PUSHPULL | PAL_STM32_OSPEED_LOWEST));
    palSetPort(PAL_PORT(A0), rowA_bits);
    palSetPort(PAL_PORT(C0), rowC_bits);
    /* Columns as input pull-up. */
    palSetGroupMode(PAL_PORT(A0), colA_bits, 0U, (PAL_STM32_MODE_INPUT | PAL_STM32_PUPDR_PULLUP | PAL_STM32_OSPEED_LOWEST));
    palSetGroupMode(PAL_PORT(B0), colB_bits, 0U, (PAL_STM32_MODE_INPUT | PAL_STM32_PUPDR_PULLUP | PAL_STM32_OSPEED_LOWEST));
}

/* Only need to scan the result into current_matrix, and return changed. */
uint8_t matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool changed = false;

    for (uint8_t current_row = 0; current_row < MATRIX_ROWS; current_row++) {
        /* Wait for all column signals to settle HIGH before selecting
         * a row.  This prevents ghost reads from the previous row scan.
         * Cap total iterations so a stuck-low column (hardware fault,
         * short) can't block the scan forever. */
        uint8_t  stable_threshold = MATRIX_DEBOUNCE;
        uint16_t settle_iters     = 0;
        while (stable_threshold > 0) {
            if (++settle_iters > MATRIX_SETTLE_MAX_ITERS) break;
            stable_threshold = ((((palReadPort(PAL_PORT(A0)) & colA_bits) ^ colA_bits) | ((palReadPort(PAL_PORT(B0)) & colB_bits) ^ colB_bits)) == 0) ? (stable_threshold - 1) : MATRIX_DEBOUNCE;
        }

        select_row(current_row);
        matrix_output_select_delay();

        matrix_row_t cols = read_cols();
        unselect_rows();

        changed |= (current_matrix[current_row] != cols);
        current_matrix[current_row] = cols;
    }

    return changed;
}
