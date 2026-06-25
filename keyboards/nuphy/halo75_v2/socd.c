// Copyright 2025 @nuphy
// SPDX-License-Identifier: GPL-2.0-or-later

#include "socd.h"
#include "halo75_v2.h"

/* Physical key state for the four cardinal directions.
 * true = physically held by the user. */
static bool phys_left = false;
static bool phys_right = false;
static bool phys_up = false;
static bool phys_down = false;

/* Which key is currently registered to the host.
 * true = sent to host and still held. */
static bool reg_left = false;
static bool reg_right = false;
static bool reg_up = false;
static bool reg_down = false;

/* Last-pressed key per axis — tracks press order for LAST_WINS/FIRST_WINS.
 * Updated in socd_process_record on every keypress. */
static uint16_t last_x = KC_NO;
static uint16_t last_y = KC_NO;

/* The active SOCD mode, mirrored from user_config.ee_socd_mode. */
static uint8_t socd_mode = SOCD_OFF;

/* ── helpers ─────────────────────────────────────────────────────── */

static bool is_arrow_key(uint16_t kc) {
    return kc == KC_LEFT || kc == KC_RIGHT || kc == KC_UP || kc == KC_DOWN;
}

static void reg_set(uint16_t kc, bool on) {
    if (on) {
        register_code(kc);
    } else {
        unregister_code(kc);
    }
}

/* ── per-axis resolution ─────────────────────────────────────────── */

/* Horizontal axis: Left vs Right.
 * Returns the keycode that should be registered (or KC_NO for neutral). */
static uint16_t resolve_x(void) {
    if (phys_left && phys_right) {
        switch (socd_mode) {
            case SOCD_NEUTRAL:    return KC_NO;
            case SOCD_LAST_WINS:  return last_x;
            case SOCD_FIRST_WINS: return (last_x == KC_LEFT) ? KC_RIGHT : KC_LEFT;
            default:              return KC_NO;
        }
    }
    if (phys_left)  return KC_LEFT;
    if (phys_right) return KC_RIGHT;
    return KC_NO;
}

/* Vertical axis: Up vs Down. */
static uint16_t resolve_y(void) {
    if (phys_up && phys_down) {
        switch (socd_mode) {
            case SOCD_NEUTRAL:    return KC_NO;
            case SOCD_LAST_WINS:  return last_y;
            case SOCD_FIRST_WINS: return (last_y == KC_UP) ? KC_DOWN : KC_UP;
            default:              return KC_NO;
        }
    }
    if (phys_up)   return KC_UP;
    if (phys_down) return KC_DOWN;
    return KC_NO;
}

/* Apply the resolved state for one axis to the host.
 * prev_reg tracks what we previously sent so we only send deltas. */
static void apply_axis(uint16_t neg_kc, uint16_t pos_kc, uint16_t resolved,
                       bool *prev_neg, bool *prev_pos) {
    bool want_neg = (resolved == neg_kc);
    bool want_pos = (resolved == pos_kc);

    if (want_neg != *prev_neg) {
        reg_set(neg_kc, want_neg);
        *prev_neg = want_neg;
    }
    if (want_pos != *prev_pos) {
        reg_set(pos_kc, want_pos);
        *prev_pos = want_pos;
    }
}

/* Recompute both axes and push deltas to the host. */
static void socd_flush(void) {
    if (socd_mode == SOCD_OFF) return;

    uint16_t x = resolve_x();
    uint16_t y = resolve_y();

    apply_axis(KC_LEFT, KC_RIGHT, x, &reg_left, &reg_right);
    apply_axis(KC_UP,   KC_DOWN,  y, &reg_up,   &reg_down);
}

/* ── public API ──────────────────────────────────────────────────── */

bool socd_process_record(uint16_t keycode, keyrecord_t *record) {
    if (socd_mode == SOCD_OFF || !is_arrow_key(keycode)) {
        return true; /* let QMK handle it normally */
    }

    bool pressed = record->event.pressed;

    /* Update physical state and press-order tracking. */
    switch (keycode) {
        case KC_LEFT:  phys_left  = pressed; if (pressed) last_x = KC_LEFT;  break;
        case KC_RIGHT: phys_right = pressed; if (pressed) last_x = KC_RIGHT; break;
        case KC_UP:    phys_up    = pressed; if (pressed) last_y = KC_UP;    break;
        case KC_DOWN:  phys_down  = pressed; if (pressed) last_y = KC_DOWN;  break;
    }

    /* Recompute and send only the changed deltas. */
    socd_flush();

    /* Block QMK's normal registration — SOCD owns these keys. */
    return false;
}

void socd_reset(void) {
    /* Unregister any keys SOCD has sent to the host. */
    if (reg_left)  { unregister_code(KC_LEFT);  reg_left  = false; }
    if (reg_right) { unregister_code(KC_RIGHT); reg_right = false; }
    if (reg_up)    { unregister_code(KC_UP);    reg_up    = false; }
    if (reg_down)  { unregister_code(KC_DOWN);  reg_down  = false; }

    phys_left = phys_right = phys_up = phys_down = false;
    last_x = KC_NO;
    last_y = KC_NO;
}

uint8_t socd_get_mode(void) {
    return socd_mode;
}

void socd_set_mode(uint8_t mode) {
    if (mode > SOCD_MODE_MAX) mode = SOCD_OFF;

    /* When switching modes, release any SOCD-managed keys first
     * to avoid stuck keys, then re-evaluate with the new mode. */
    if (reg_left)  { unregister_code(KC_LEFT);  reg_left  = false; }
    if (reg_right) { unregister_code(KC_RIGHT); reg_right = false; }
    if (reg_up)    { unregister_code(KC_UP);    reg_up    = false; }
    if (reg_down)  { unregister_code(KC_DOWN);  reg_down  = false; }

    socd_mode = mode;

    /* If physical keys are still held, re-register under new rules. */
    if (mode != SOCD_OFF) {
        socd_flush();
    }
}
