// Copyright 2025 @nuphy
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"

/* SOCD (Simultaneous Opposite Cardinal Directions) resolution modes.
 *
 * When opposing arrow keys (Left+Right or Up+Down) are held simultaneously,
 * the mode determines which input the host receives:
 *
 *   OFF         – no resolution, both keys pass through to the host
 *   NEUTRAL     – opposing keys cancel; neither is sent
 *   LAST_WINS   – the most recently pressed direction wins (2IP, fighting-game standard)
 *   FIRST_WINS  – the first pressed direction wins until it is released
 */
enum socd_mode {
    SOCD_OFF        = 0,
    SOCD_NEUTRAL    = 1,
    SOCD_LAST_WINS  = 2,
    SOCD_FIRST_WINS = 3,
};
#define SOCD_MODE_MAX SOCD_FIRST_WINS

/* Intercept arrow keycodes in process_record_kb.
 * Returns true to allow normal QMK processing, false to block (SOCD handles registration). */
bool socd_process_record(uint16_t keycode, keyrecord_t *record);

/* Reset all SOCD tracking state and unregister any SOCD-managed keys.
 * Call from m_break_all_key() or similar full-reset paths. */
void socd_reset(void);

/* Runtime getters / setters (persist via eeconfig_update_user_datablock). */
uint8_t socd_get_mode(void);
void    socd_set_mode(uint8_t mode);
