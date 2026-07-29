// Copyright 2026 Ellie
// SPDX-License-Identifier: GPL-2.0-or-later

#include "rgb_matrix.h"
#include "is31fl3733.h"
#include "halo75_v2_internal.h"

/*
 * The Halo has two independent IS31FL3733s. QMK's stock flush writes them
 * consecutively, creating one combined matrix-scan blackout. Keep the stock
 * chip implementation and only own the keyboard-specific scheduling policy:
 * sample the matrix at the natural boundary between the two transfers.
 */
static void halo75_v2_rgb_matrix_flush(void) {
    for (uint8_t driver = 0; driver < IS31FL3733_DRIVER_COUNT; driver++) {
        is31fl3733_update_pwm_buffers(driver);

        if (driver + 1 < IS31FL3733_DRIVER_COUNT) {
            halo75_v2_matrix_capture();
        }
    }
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = is31fl3733_init_drivers,
    .flush         = halo75_v2_rgb_matrix_flush,
    .set_color     = is31fl3733_set_color,
    .set_color_all = is31fl3733_set_color_all,
};
