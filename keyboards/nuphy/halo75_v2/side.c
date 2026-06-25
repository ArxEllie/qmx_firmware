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
#include "side.h"
#include "gpio.h"
#ifdef RGB_DEBUG
#    include "print.h"
#endif
//------------------------------------------------
#define SIDE_WAVE 0
#define SIDE_MIX 1
#define SIDE_NEW 2
#define SIDE_BREATH 3
#define SIDE_STATIC 4

#define SIDE_MODE_1 0
#define SIDE_MODE_2 1
#define SIDE_MODE_3 2
#define SIDE_MODE_4 3
#define SIDE_MODE_5 4
#define SIDE_MODE_6 5
#define SIDE_MODE_7 6

#define LIGHT_COLOUR_MAX 8
#define SIDE_COLOUR_MAX 8
#define LIGHT_SPEED_MAX 4

// Minimum interval (ms) between full side LED refreshes.
// rgb_matrix_indicators_advanced_user() can fire several times per render
// cycle (once per LED_PROCESS_LIMIT iteration).  The animation modes already
// self-throttle via side_play_cnt, and the status LED functions use their own
// internal timers, so capping the whole pipeline at ~60 fps avoids redundant
// rgb_matrix_set_color() writes for static indicators and saves battery on
// wireless.
#define SIDE_REFRESH_INTERVAL 16

const uint8_t side_speed_table[5][5] = {
    [SIDE_WAVE] = {10, 20, 25, 30, 45}, [SIDE_MIX] = {25, 30, 40, 50, 60}, [SIDE_NEW] = {30, 50, 60, 70, 100}, [SIDE_BREATH] = {25, 30, 40, 50, 60}, [SIDE_STATIC] = {10, 20, 25, 30, 45},
};

#define SIDE_BLINK_LIGHT 128
const uint8_t side_light_table[5] = {
    0, 64, 128, 192, 255,
};

const uint8_t light_value_tab[101] = {
    0, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 36, 37, 39, 40, 42, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87, 89, 91, 94, 96, 99, 101, 104, 106, 109, 111, 114, 116, 119, 121, 124, 126, 129, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 161, 164, 167, 170, 173, 176, 179, 182, 185, 188, 191, 194, 197, 200, 203, 206, 209, 213, 216, 220, 223, 227, 230, 234, 237, 241, 245, 248, 251, 255, 255, 255,
};

const uint8_t breathe_data_tab[BREATHE_TAB_LEN] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  12,  14,  16,  18,  20,  22,  24,  27,  30,  33,  36,  39,  42,  45,  49,  53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 94, 99, 104, 109, 114, 119, 124, 129, 134, 140, 146, 152, 158, 164, 170, 176, 182, 188, 194, 200, 206, 213, 220, 227, 234, 241, 248, 255,

    255, 248, 241, 234, 227, 220, 213, 206, 200, 194, 188, 182, 176, 170, 164, 158, 152, 146, 140, 134, 129, 124, 119, 114, 109, 104, 99, 94, 89, 85, 81, 77, 73, 69, 65, 61, 57, 53, 49,  45,  42,  39,  36,  33,  30,  27,  24,  22,  20,  18,  16,  14,  12,  10,  9,   8,   7,   6,   5,   4,   3,   2,   1,   0,
};

const uint8_t wave_data_tab[WAVE_TAB_LEN] = {
    22,  23,  24,  25,  27,  28,  30,  31,  33,  34,  36,  37,  39,  40,  42,  43,  45,  47,  49,  51,  53,  55,  57,  59,  61,  63,  65, 76, 69, 71, 73, 75, 77, 81, 85, 89, 94, 99, 104, 109, 114, 119, 124, 129, 134, 140, 146, 152, 158, 164, 170, 176, 182, 188, 194, 200, 206, 213, 220, 227, 234, 241, 248, 255,

    255, 248, 241, 234, 227, 220, 213, 206, 200, 194, 188, 182, 176, 170, 164, 158, 152, 146, 140, 134, 129, 124, 119, 114, 109, 104, 99, 94, 89, 85, 81, 77, 73, 69, 65, 61, 57, 53, 49,  45,  42,  39,  36,  33,  30,  27,  24,  22,
};

const uint8_t flow_rainbow_colour_tab[FLOW_COLOUR_TAB_LEN][3] = {
    {255, 8, 8}, {255, 8, 8}, {255, 8, 8}, {255, 8, 8}, {255, 10, 8}, {255, 14, 8}, {255, 18, 8}, {255, 22, 8},
    {255, 26, 8}, {255, 32, 8}, {255, 38, 8}, {255, 44, 8}, {255, 50, 8}, {255, 57, 8}, {255, 65, 8}, {255, 73, 8},
    {255, 81, 8}, {255, 89, 8}, {255, 99, 8}, {255, 109, 8}, {255, 119, 8}, {255, 129, 8}, {255, 140, 8}, {255, 152, 8},
    {255, 164, 8}, {255, 176, 8}, {255, 188, 8}, {255, 200, 8}, {255, 213, 8}, {255, 227, 8}, {255, 241, 8}, {255, 255, 8},
    {248, 255, 8}, {234, 255, 8}, {220, 255, 8}, {206, 255, 8}, {194, 255, 8}, {182, 255, 8}, {170, 255, 8}, {158, 255, 8},
    {146, 255, 8}, {134, 255, 8}, {124, 255, 8}, {114, 255, 8}, {104, 255, 8}, {94, 255, 8}, {85, 255, 8}, {77, 255, 8},
    {69, 255, 8}, {61, 255, 8}, {53, 255, 8}, {47, 255, 8}, {41, 255, 8}, {35, 255, 8}, {29, 255, 8}, {24, 255, 8},
    {20, 255, 8}, {16, 255, 8}, {12, 255, 8}, {8, 255, 8}, {8, 255, 8}, {8, 255, 8}, {8, 255, 8}, {8, 255, 8},
    {8, 255, 8}, {8, 255, 8}, {8, 255, 8}, {8, 255, 8}, {8, 255, 10}, {8, 255, 14}, {8, 255, 18}, {8, 255, 22},
    {8, 255, 26}, {8, 255, 32}, {8, 255, 38}, {8, 255, 44}, {8, 255, 50}, {8, 255, 57}, {8, 255, 65}, {8, 255, 73},
    {8, 255, 81}, {8, 255, 89}, {8, 255, 99}, {8, 255, 109}, {8, 255, 119}, {8, 255, 129}, {8, 255, 140}, {8, 255, 152},
    {8, 255, 164}, {8, 255, 176}, {8, 255, 188}, {8, 255, 200}, {8, 255, 213}, {8, 255, 227}, {8, 255, 241}, {8, 255, 255},
    {8, 248, 255}, {8, 234, 255}, {8, 220, 255}, {8, 206, 255}, {8, 194, 255}, {8, 182, 255}, {8, 170, 255}, {8, 158, 255},
    {8, 146, 255}, {8, 134, 255}, {8, 124, 255}, {8, 114, 255}, {8, 104, 255}, {8, 94, 255}, {8, 85, 255}, {8, 77, 255},
    {8, 69, 255}, {8, 61, 255}, {8, 53, 255}, {8, 47, 255}, {8, 41, 255}, {8, 35, 255}, {8, 29, 255}, {8, 24, 255},
    {8, 20, 255}, {8, 16, 255}, {8, 12, 255}, {8, 8, 255}, {8, 8, 255}, {8, 8, 255}, {8, 8, 255}, {8, 8, 255},
    {8, 8, 255}, {8, 8, 255}, {8, 8, 255}, {8, 8, 255}, {10, 8, 255}, {14, 8, 255}, {18, 8, 255}, {22, 8, 255},
    {26, 8, 255}, {32, 8, 255}, {38, 8, 255}, {44, 8, 255}, {50, 8, 255}, {57, 8, 255}, {65, 8, 255}, {73, 8, 255},
    {81, 8, 255}, {89, 8, 255}, {99, 8, 255}, {109, 8, 255}, {119, 8, 255}, {129, 8, 255}, {140, 8, 255}, {152, 8, 255},
    {164, 8, 255}, {176, 8, 255}, {188, 8, 255}, {200, 8, 255}, {213, 8, 255}, {227, 8, 255}, {241, 8, 255}, {255, 8, 255},
    {255, 8, 248}, {255, 8, 234}, {255, 8, 220}, {255, 8, 206}, {255, 8, 194}, {255, 8, 182}, {255, 8, 170}, {255, 8, 158},
    {255, 8, 146}, {255, 8, 134}, {255, 8, 124}, {255, 8, 114}, {255, 8, 104}, {255, 8, 94}, {255, 8, 85}, {255, 8, 77},
    {255, 8, 69}, {255, 8, 61}, {255, 8, 53}, {255, 8, 47}, {255, 8, 41}, {255, 8, 35}, {255, 8, 29}, {255, 8, 24},
    {255, 8, 20}, {255, 8, 16}, {255, 8, 12}, {255, 8, 8}, {255, 8, 8}, {255, 8, 8}, {255, 8, 8}, {255, 8, 8},
};

const uint8_t dual_colour_lib[3][6] = {
    {0, 255, 255, 0, 0, 255},
    {0, 0, 255, 255, 0, 160},
    {0, 255, 255, 255, 64, 64},
};

const uint8_t colour_lib[9][3] = {
    {0xff, 0x00, 0x00}, {0xff, 0x20, 0x00}, {0x80, 0x40, 0x00}, {0x00, 0x80, 0x00}, {0x00, 0x80, 0x80}, {0x00, 0x00, 0x80}, {0xff, 0x00, 0xa0}, {0xff, 0xa0, 0xa0}, {0x00, 0x00, 0x00},
};
const uint8_t colour_lib_1[9][3] = {
    {0x70, 0x00, 0x00}, {0x70, 0x20, 0x00}, {0x60, 0x30, 0x00}, {0x00, 0x70, 0x00}, {0x00, 0x70, 0x70}, {0x00, 0x00, 0x70}, {0x70, 0x00, 0x70}, {0x40, 0x70, 0x60}, {0x00, 0x00, 0x00},
};

uint8_t  side_line       = SIDE_LED_COUNT;
bool     f_charging      = 0;
uint8_t  side_mode_a     = 0;
uint8_t  side_mode_b     = 3;
uint8_t  side_light      = 2;
uint8_t  side_speed      = 2;
uint8_t  side_rgb        = 1;
uint8_t  side_colour     = 0;
uint8_t  side_play_point = 0;
uint32_t bat_show_time   = 0;
bool     bat_show_flag   = true;
uint16_t side_play_cnt   = 0;
uint32_t side_play_timer = 0;

uint8_t r_temp, g_temp, b_temp;

/**
 * @brief suspend_power_down_kb
 *
 */
void suspend_power_down_kb(void) {
    rgb_matrix_set_suspend_state(true);
}

/**
 * @brief suspend_wakeup_init_kb
 *
 */
void suspend_wakeup_init_kb(void) {
    rgb_matrix_set_suspend_state(false);
}
/**
 * @brief  Adjusting the brightness of side lights.
 * @param  dir: 0 - decrease, 1 - increase.
 * @note  save to eeprom.
 */
void light_level_control(uint8_t brighten) {
    if (brighten) {
        if (side_light == 4) {
            return;
        } else
            side_light++;
    } else {
        if (side_light == 0) {
            return;
        } else
            side_light--;
    }
    side_led_set_light(side_light);
    user_config_mark_dirty();
}

/**
 * @brief  Adjusting the speed of side lights.
 * @param  dir: 0 - decrease, 1 - increase.
 * @note  save to eeprom.
 */
void light_speed_control(uint8_t fast) {
    if ((side_speed) > LIGHT_SPEED_MAX) (side_speed) = LIGHT_SPEED_MAX / 2;

    if (fast) {
        if ((side_speed)) side_speed--;
    } else {
        if ((side_speed) < LIGHT_SPEED_MAX) side_speed++;
    }
    side_led_set_speed(side_speed);
    user_config_mark_dirty();
}

/**
 * @brief  Switch to the next color of side lights.
 * @param  dir: 0 - prev, 1 - next.
 * @note  save to eeprom.
 */
uint8_t light_colour_max = 8;
void    side_colour_control(uint8_t dir) {
    if (side_mode_a == SIDE_NEW)
        light_colour_max = 3;
    else
        light_colour_max = 8;
    if ((side_mode_a != SIDE_WAVE) && (side_mode_a != SIDE_BREATH)) {
        if (side_rgb) {
            side_rgb    = 0;
            side_colour = 0;
        }
    }

    if (dir) {
        if (side_rgb) {
            side_rgb    = 0;
            side_colour = 0;
        } else {
            side_colour++;
            if (side_colour >= light_colour_max) {
                side_rgb    = 1;
                side_colour = 0;
            }
        }
    } else {
        if (side_rgb) {
            side_rgb    = 0;
            side_colour = light_colour_max - 1;
        } else {
            side_colour--;
            if (side_colour >= light_colour_max) {
                side_rgb    = 1;
                side_colour = 0;
            }
        }
    }
    side_led_set_rgb(side_rgb);
    side_led_set_colour(side_colour);
    user_config_mark_dirty();
}

/**
 * @brief  Change the color mode of side lights.
 * @param  dir: 0 - prev, 1 - next.
 * @note  save to eeprom.
 */
uint8_t side_old_color = 0;
void    side_mode_a_control(uint8_t dir) {
    if (dir) {
        side_mode_a++;
        if (side_mode_a > SIDE_STATIC) {
            side_mode_a = 0;
        }
    } else {
        if (side_mode_a > 0) {
            side_mode_a--;
        } else {
            side_mode_a = 0;
        }
    }
    if (side_mode_a == SIDE_NEW) {
        side_old_color = side_colour;
        side_colour    = 0;
    } else if (side_mode_a == SIDE_BREATH) {
        side_colour = side_old_color;
    }

    side_play_point            = 0;
    side_led_set_mode_a(side_mode_a);
    user_config_mark_dirty();
}

void side_mode_b_control(uint8_t dir) {
    if (dir) {
        side_mode_b++;
        if (side_mode_b > SIDE_MODE_7) {
            side_mode_b = SIDE_MODE_1;
        }
    } else {
        if (side_mode_b > 0) {
            side_mode_b--;
        } else {
            side_mode_b = SIDE_MODE_1;
        }
    }
    side_play_point            = 0;
    side_led_set_mode_b(side_mode_b);
    user_config_mark_dirty();
}

/**
 * @brief  set left side leds.
 * @param  ...
 */
static rgb_t side_status_rgb[5] = {0};

static void set_left_rgb_no_cache(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 5; i++)
        rgb_matrix_set_color(SIDE_INDEX + i, r, g, b);
}

static void side_status_led_set(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 5) return;

    side_status_rgb[index] = (rgb_t){r, g, b};
    side_bat_led_set(index, r, g, b);
}

void set_left_rgb(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 5; i++)
        side_status_rgb[i] = (rgb_t){r, g, b};

    set_left_rgb_no_cache(r, g, b);
}

static void restore_left_rgb(void) {
    for (int i = 0; i < 5; i++)
        rgb_matrix_set_color(SIDE_INDEX + i, side_status_rgb[i].r, side_status_rgb[i].g, side_status_rgb[i].b);
}

void set_all_side_off(void) {
    for (int i = SIDE_RIM_START; i < SIDE_LED_COUNT; i++)
        rgb_matrix_set_color(side_led_index_tab[i], 0, 0, 0);
}

/**
 * @brief  mac or win system indicate
 */
void sys_sw_led_show(void) {
    static uint32_t sys_show_timer = 0;
    static bool     sys_show_flag  = false;
    extern bool     f_sys_show;

    if (f_sys_show) {
        f_sys_show     = false;
        sys_show_timer = timer_read32(); // store time of last refresh
        sys_show_flag  = true;
    }

    if (sys_show_flag) {
        if (dev_info.sys_sw_state == SYS_SW_MAC) {
            r_temp = colour_lib[7][0];
            g_temp = colour_lib[7][1];
            b_temp = colour_lib[7][2];
        } else {
            r_temp = colour_lib[5][0];
            g_temp = colour_lib[5][1];
            b_temp = colour_lib[5][2];
        }
        if ((timer_elapsed32(sys_show_timer) / 500) % 2 == 0) {
            set_left_rgb(r_temp, g_temp, b_temp);
        } else {
            set_left_rgb(0x00, 0x00, 0x00);
        }
        if (timer_elapsed32(sys_show_timer) >= (3000 - 50)) {
            sys_show_flag = false;
        }
    }
}

/**
 * @brief  sleep enable or disable indicate
 */
void sleep_sw_led_show(void) {
    static uint32_t sleep_show_timer = 0;
    static bool     sleep_show_flag  = false;
    extern bool     f_sleep_show;

    if (f_sleep_show) {
        f_sleep_show     = false;
        sleep_show_timer = timer_read32(); // store time of last refresh
        sleep_show_flag  = true;
    }

    if (sleep_show_flag) {
        if (f_dev_sleep_enable) {
            r_temp = 0x00;
            g_temp = SIDE_BLINK_LIGHT;
            b_temp = 0x00;
        } else {
            r_temp = 0xff;
            g_temp = 0x00;
            b_temp = 0x00;
        }
        if ((timer_elapsed32(sleep_show_timer) / 500) % 2 == 0) {
            set_left_rgb(r_temp, g_temp, b_temp);
        } else {
            set_left_rgb(0x00, 0x00, 0x00);
        }
        if (timer_elapsed32(sleep_show_timer) >= (3000 - 50)) {
            sleep_show_flag = false;
        }
    }
}

/**
 * @brief  host system led indicate.
 */
void sys_led_show(void) {
    static bool was_on = false;
    bool        is_on  = false;

    if (dev_info.link_mode == LINK_USB) {
        is_on = host_keyboard_led_state().caps_lock;
    } else {
        is_on = dev_info.rf_led & 0x02;
    }

    if (is_on) {
        // ponytail: Caps/RF is a temporary overlay; keep the cached base status
        // color intact so the falling edge can restore it without a second render.
        set_left_rgb_no_cache(colour_lib[4][0], colour_lib[4][1], colour_lib[4][2]);
    } else if (was_on) {
        restore_left_rgb();
    }

    was_on = is_on;
}

/**
 * @brief  light_point_playing.
 * @param trend:
 * @param step:
 * @param len:
 * @param point:
 */
static void light_point_playing(uint8_t trend, uint8_t step, uint8_t len, uint8_t *point) {
    if (trend) {
        *point += step;
        if (*point >= len) *point -= len;
    } else {
        *point -= step;
        if (*point >= len) *point = len - (255 - *point) - 1;
    }
}

/**
 * @brief  count_rgb_light.
 * @param light_temp:
 */
static void count_rgb_light(uint8_t light_temp) {
    uint16_t temp;

    temp   = (light_temp)*r_temp + r_temp;
    r_temp = temp >> 8;

    temp   = (light_temp)*g_temp + g_temp;
    g_temp = temp >> 8;

    temp   = (light_temp)*b_temp + b_temp;
    b_temp = temp >> 8;
}

/**
 * @brief  auxiliary_rgb_light.
 */
uint8_t f_side_flag      = 0x1f;
uint8_t key_pwm_tab[45]  = {0x00};
uint8_t power_play_index = 0;
uint8_t f_power_show     = 1;

static void side_power_mode_show(void) {
    if (side_play_cnt <= side_speed_table[0][side_speed])
        return;
    else
        side_play_cnt -= side_speed_table[0][side_speed];
    if (side_play_cnt > 20) side_play_cnt = 0;

    if (power_play_index < 45) {
        if (power_play_index >= SIDE_RIM_START && power_play_index < SIDE_LED_COUNT) {
            key_pwm_tab[power_play_index] = 0xff;
        }
        power_play_index++;
    }

    uint8_t i;

    for (i = SIDE_RIM_START; i < SIDE_LED_COUNT; i++) {
        r_temp = colour_lib[side_colour][0];
        g_temp = colour_lib[side_colour][1];
        b_temp = colour_lib[side_colour][2];

        count_rgb_light(key_pwm_tab[i]);
        count_rgb_light(side_light_table[2]);
        rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
    }

    for (i = SIDE_RIM_START; i < SIDE_LED_COUNT; i++) {
        if (key_pwm_tab[i] & 0x80)
            key_pwm_tab[i] -= 8;
        else if (key_pwm_tab[i] & 0x40)
            key_pwm_tab[i] -= 6;
        else if (key_pwm_tab[i] & 0x20)
            key_pwm_tab[i] -= 4;
        else if (key_pwm_tab[i] & 0x10)
            key_pwm_tab[i] -= 3;
        else if (key_pwm_tab[i] & 0x08)
            key_pwm_tab[i] -= 2;
        else if (key_pwm_tab[i])
            key_pwm_tab[i]--;
    }

    if (key_pwm_tab[SIDE_LED_COUNT - 1] == 1) {
        f_power_show      = 0;
        rf_link_show_time = 0;
        bat_show_flag     = true;
        f_charging        = true;
        bat_show_time     = timer_read32();
    }
}

/**
 * @brief  side_wave_mode_show.
 */
static void side_wave_mode_show(void) {
    uint8_t play_index;
    uint8_t play_index_1;

    if (side_play_cnt <= side_speed_table[side_mode_a][side_speed])
        return;
    else
        side_play_cnt -= side_speed_table[side_mode_a][side_speed];
    if (side_play_cnt > 20) side_play_cnt = 0;

    if (side_rgb)
        light_point_playing(0, 1, FLOW_COLOUR_TAB_LEN, &side_play_point);
    else
        light_point_playing(0, 1, WAVE_TAB_LEN, &side_play_point);

    play_index = side_play_point;
    if (side_line == 0) set_all_side_off();
    for (int i = SIDE_RIM_START; i <= SIDE_ANIM_LOOP_END; i++) {
        if (side_rgb) {
            r_temp = flow_rainbow_colour_tab[play_index][0];
            g_temp = flow_rainbow_colour_tab[play_index][1];
            b_temp = flow_rainbow_colour_tab[play_index][2];

            light_point_playing(1, 5, FLOW_COLOUR_TAB_LEN, &play_index);

        } else {
            r_temp = colour_lib[side_colour][0];
            g_temp = colour_lib[side_colour][1];
            b_temp = colour_lib[side_colour][2];

            light_point_playing(1, 5, WAVE_TAB_LEN, &play_index);
            count_rgb_light(wave_data_tab[play_index]);
        }

        count_rgb_light(side_light_table[side_light]);

        play_index_1 = play_index;

        if (i == 40) {
            if (f_side_flag == 0x1f) {
                for (; i < SIDE_LED_COUNT; i++) {
                    if (side_rgb) {
                        r_temp = flow_rainbow_colour_tab[play_index_1][0] * 0.4;
                        g_temp = flow_rainbow_colour_tab[play_index_1][1] * 0.4;
                        b_temp = flow_rainbow_colour_tab[play_index_1][2] * 0.4;
                    } else {
                        r_temp = colour_lib[side_colour][0] * 0.4;
                        g_temp = colour_lib[side_colour][1] * 0.4;
                        b_temp = colour_lib[side_colour][2] * 0.4;
                        count_rgb_light(wave_data_tab[play_index_1]);
                    }
                    count_rgb_light(side_light_table[side_light]);
                    rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
                }
                return;
            } else {
                for (; i < SIDE_LED_COUNT; i++) {
                    side_rgb_off(i);
                }
                return;
            }
        }
        if (is_side_rgb_on(i))
            rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
        else
            side_rgb_off(i);
    }
}

static void side_new_mode_show(void) {
    uint8_t play_index;

    if (side_line <= 5) {
        set_all_side_off();
        return;
    }

    if (side_play_cnt <= side_speed_table[side_mode_a][side_speed])
        return;
    else
        side_play_cnt -= side_speed_table[side_mode_a][side_speed];
    if (side_play_cnt > 20) side_play_cnt = 0;

    const uint8_t side_anim_line = side_line - 5;

    light_point_playing(0, 1, side_anim_line, &side_play_point);
    play_index = side_play_point;
    for (int i = SIDE_RIM_START; i <= SIDE_ANIM_LOOP_END; i++) {
        if (play_index < side_anim_line / 2) {
            r_temp = dual_colour_lib[side_colour][0];
            g_temp = dual_colour_lib[side_colour][1];
            b_temp = dual_colour_lib[side_colour][2];
        } else {
            r_temp = dual_colour_lib[side_colour][3];
            g_temp = dual_colour_lib[side_colour][4];
            b_temp = dual_colour_lib[side_colour][5];
        }

        light_point_playing(1, 1, side_anim_line, &play_index);

        count_rgb_light(side_light_table[side_light]);

        if (i == 40) {
            if (f_side_flag == 0x1f) {
                r_temp = r_temp * 0.3;
                g_temp = g_temp * 0.3;
                b_temp = b_temp * 0.3;

                for (; i < SIDE_LED_COUNT; i++) {
                    rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
                }
                return;
            } else {
                for (; i < SIDE_LED_COUNT; i++) {
                    side_rgb_off(i);
                }
                return;
            }
        }
        if (is_side_rgb_on(i))
            rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
        else
            side_rgb_off(i);
    }
}

static void side_spectrum_mode_show(void) {
    if (side_play_cnt <= side_speed_table[side_mode_a][side_speed])
        return;
    else
        side_play_cnt -= side_speed_table[side_mode_a][side_speed];
    if (side_play_cnt > 20) side_play_cnt = 0;

    if (side_line == 0) set_all_side_off();

    light_point_playing(1, 1, FLOW_COLOUR_TAB_LEN, &side_play_point);

    r_temp = flow_rainbow_colour_tab[side_play_point][0];
    g_temp = flow_rainbow_colour_tab[side_play_point][1];
    b_temp = flow_rainbow_colour_tab[side_play_point][2];

    count_rgb_light(side_light_table[side_light]);

    for (int i = SIDE_RIM_START; i <= SIDE_ANIM_LOOP_END; i++) {
        if (i == 40) {
            if (f_side_flag == 0x1f) {
                r_temp = r_temp * 0.3;
                g_temp = g_temp * 0.3;
                b_temp = b_temp * 0.3;
                for (; i < SIDE_LED_COUNT; i++)
                    rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
                return;
            } else {
                for (; i < SIDE_LED_COUNT; i++)
                    side_rgb_off(i);
                return;
            }
        }
        if (is_side_rgb_on(i))
            rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
        else
            side_rgb_off(i);
    }
}

static void side_pride_rainbow_show(void) {
    // This is deliberately slower than the built-in rainbow side modes: a
    // broad moving ribbon reads as pride rainbow without turning the rim into
    // a busy chase animation.
    if (side_play_cnt <= 70) return;
    side_play_cnt -= 70;
    if (side_play_cnt > 20) side_play_cnt = 0;

    light_point_playing(1, 1, FLOW_COLOUR_TAB_LEN, &side_play_point);

    uint8_t play_index = side_play_point;
    for (int i = SIDE_RIM_START; i <= SIDE_ANIM_LOOP_END; i++) {
        r_temp = flow_rainbow_colour_tab[play_index][0];
        g_temp = flow_rainbow_colour_tab[play_index][1];
        b_temp = flow_rainbow_colour_tab[play_index][2];

        // ponytail: one shared calm rainbow for both pride effects. If the
        // side LEDs ever get denser, upgrade this step to a geometry-based
        // gradient instead of table index spacing.
        light_point_playing(1, 3, FLOW_COLOUR_TAB_LEN, &play_index);

        count_rgb_light(side_light_table[side_light]);

        if (i == 40) {
            for (; i < SIDE_LED_COUNT; i++) {
                r_temp = flow_rainbow_colour_tab[play_index][0] * 0.3;
                g_temp = flow_rainbow_colour_tab[play_index][1] * 0.3;
                b_temp = flow_rainbow_colour_tab[play_index][2] * 0.3;
                count_rgb_light(side_light_table[side_light]);
                rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
            }
            return;
        }

        if (is_side_rgb_on(i)) {
            rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
        } else {
            side_rgb_off(i);
        }
    }
}

static void side_breathe_mode_show(void) {
    static uint8_t play_point = 0;
    static uint8_t colour     = 0;

    if (side_play_cnt <= side_speed_table[side_mode_a][side_speed])
        return;
    else
        side_play_cnt -= side_speed_table[side_mode_a][side_speed];
    if (side_play_cnt > 20) side_play_cnt = 0;

    if (side_line == 0) set_all_side_off();

    light_point_playing(0, 1, BREATHE_TAB_LEN, &play_point);

    if (side_rgb) {
        if (play_point == 0) {
            if (++colour >= LIGHT_COLOUR_MAX) colour = 0;
        }
        r_temp = colour_lib[colour][0];
        g_temp = colour_lib[colour][1];
        b_temp = colour_lib[colour][2];

    } else {
        r_temp = colour_lib[side_colour][0];
        g_temp = colour_lib[side_colour][1];
        b_temp = colour_lib[side_colour][2];
    }
    count_rgb_light(breathe_data_tab[play_point]);
    count_rgb_light(side_light_table[side_light]);

    for (int i = SIDE_RIM_START; i <= SIDE_ANIM_LOOP_END; i++) {
        if (i == 40) {
            if (f_side_flag == 0x1f) {
                r_temp = r_temp * 0.3;
                g_temp = g_temp * 0.3;
                b_temp = b_temp * 0.3;
                for (; i < SIDE_LED_COUNT; i++)
                    rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
                return;
            } else {
                for (; i < SIDE_LED_COUNT; i++)
                    side_rgb_off(i);
                return;
            }
        }
        if (is_side_rgb_on(i))
            rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
        else
            side_rgb_off(i);
    }
}

/**
 * @brief  side_static_mode_show.
 */
static void side_static_mode_show(void) {
    if (side_play_cnt <= side_speed_table[side_mode_a][side_speed])
        return;
    else
        side_play_cnt -= side_speed_table[side_mode_a][side_speed];
    if (side_play_cnt > 20) side_play_cnt = 0;

    if (side_line == 0) set_all_side_off();

    if (side_play_point >= SIDE_COLOUR_MAX) side_play_point = 0;

    for (int i = SIDE_RIM_START; i < SIDE_LED_COUNT; i++) {
        r_temp = colour_lib[side_colour][0];
        g_temp = colour_lib[side_colour][1];
        b_temp = colour_lib[side_colour][2];

        if ((side_led_index_tab[i] <= SIDE_INDEX + 9) && (side_led_index_tab[i] >= SIDE_INDEX)) {
            r_temp = colour_lib_1[side_colour][0] * 0.7;
            g_temp = colour_lib_1[side_colour][1] * 0.7;
            b_temp = colour_lib_1[side_colour][2] * 0.7;
        }

        count_rgb_light(side_light_table[side_light]);

        if (is_side_rgb_on(i))
            rgb_matrix_set_color(side_led_index_tab[i], r_temp, g_temp, b_temp);
        else
            side_rgb_off(i);
    }
}

/**
 * @brief  bat_chargeing_breathe.
 */
void bat_charging_breathe(void) {
    static uint32_t interval_timer = 0;
    static uint8_t  play_point     = 0;

    if (timer_elapsed32(interval_timer) > 30) {
        interval_timer = timer_read32();
        light_point_playing(0, 2, BREATHE_TAB_LEN, &play_point);
    }

    r_temp = 0x80;
    g_temp = 0x40;
    b_temp = 0x00;
    count_rgb_light(breathe_data_tab[play_point]);
    set_left_rgb(r_temp, g_temp, b_temp);
}

/**
 * @brief  bat_chargeing_design.
 */
void bat_charging_design(uint8_t init, uint8_t r, uint8_t g, uint8_t b) {
    static uint32_t interval_timer = 0;
    static uint16_t show_mask      = 0x00;
    static bool     f_move_trend   = 0;
    uint16_t        bit_mask       = 1;
    uint8_t         i;

    if (timer_elapsed32(interval_timer) > 100) {
        interval_timer = timer_read32();

        if (f_move_trend) {
            show_mask >>= 1;
            if (show_mask == 0x1f >> (side_line - init)) f_move_trend = 0;
        } else {
            show_mask <<= 1;
            show_mask |= 1;
            if (show_mask == 0x7f) f_move_trend = 1;
        }
    }

    for (i = SIDE_RIM_START; i < side_line; i++) {
        if (show_mask & bit_mask) {
            rgb_matrix_set_color(side_led_index_tab[i], r, g, b);
        } else {
            rgb_matrix_set_color(side_led_index_tab[i], 0x00, 0x00, 0x00);
        }
        bit_mask <<= 1;
    }
}

/**
 * @brief  rf state indicate
 */
#define RF_LED_LINK_PERIOD 500
#define RF_LED_PAIR_PERIOD 250
void rf_led_show(void) {
    static uint32_t rf_blink_timer = 0;
    static bool     flag_power_on  = 1;
    uint16_t        rf_blink_priod = 0;

    if (dev_info.link_mode == LINK_RF_24) {
        r_temp = colour_lib[3][0];
        g_temp = colour_lib[3][1];
        b_temp = colour_lib[3][2];
    } else if (dev_info.link_mode == LINK_USB) {
        r_temp = colour_lib[2][0];
        g_temp = colour_lib[2][1];
        b_temp = colour_lib[2][2];
        if (flag_power_on && (rf_link_show_time < RF_LINK_SHOW_TIME)) return;
    } else {
        r_temp = colour_lib[5][0];
        g_temp = colour_lib[5][1];
        b_temp = colour_lib[5][2];
    }

    flag_power_on = 0;

    if (rf_blink_cnt) {
        if (dev_info.rf_state == RF_PAIRING)
            rf_blink_priod = RF_LED_PAIR_PERIOD;
        else
            rf_blink_priod = RF_LED_LINK_PERIOD;

        if (timer_elapsed32(rf_blink_timer) < (rf_blink_priod >> 1)) {
        } else {
            r_temp = 0x00;
            g_temp = 0x00;
            b_temp = 0x00;
        }

        if (timer_elapsed32(rf_blink_timer) >= rf_blink_priod) {
            rf_blink_cnt--;
            rf_blink_timer = timer_read32();
        }
    } else if (rf_link_show_time < RF_LINK_SHOW_TIME) {
    } else {
        rf_blink_timer = timer_read32();
        return;
    }

    set_left_rgb(r_temp, g_temp, b_temp);
}

uint8_t low_bat_blink_cnt = 6;
#define LOW_BAT_BLINK_PRIOD 500
void low_bat_show(void) {
    static uint32_t interval_timer = 0;

    r_temp = 0x80, g_temp = 0, b_temp = 0;

    if (low_bat_blink_cnt) {
        if (timer_elapsed32(interval_timer) > (LOW_BAT_BLINK_PRIOD >> 1)) {
            r_temp = 0x00;
            g_temp = 0x00;
            b_temp = 0x00;
        }

        if (timer_elapsed32(interval_timer) >= LOW_BAT_BLINK_PRIOD) {
            interval_timer = timer_read32();
            low_bat_blink_cnt--;
        }
    }
    set_left_rgb(r_temp, g_temp, b_temp);
}

uint8_t bat_pwm_buf[6 * 3] = {0};
uint8_t bat_end_led        = 0;
uint8_t bat_r, bat_g, bat_b;

/**
 * @brief  Battery level indicator — ported from NuPhy Halo65 V2 (f1856912d6).
 *
 * Matches the official manual's behaviour:
 *  - All 5 status LEDs light the same colour based on battery level.
 *  - Segment count only affects the charging animation, not the steady display.
 *  - Colour thresholds: ≤20% red, ≤50% orange-red, ≤80% dark orange, >80% green.
 *  - Low battery (<10%): red blink.  Charging: breathe or shift animation.
 */
void bat_percent_led(uint8_t bat_percent) {
    uint8_t i;

    if (bat_percent <= 20) {
        bat_end_led = 1;
        bat_r = colour_lib[0][0]; bat_g = colour_lib[0][1]; bat_b = colour_lib[0][2];
    } else if (bat_percent <= 50) {
        bat_end_led = 2;
        bat_r = colour_lib[1][0]; bat_g = colour_lib[1][1]; bat_b = colour_lib[1][2];
    } else if (bat_percent <= 80) {
        bat_end_led = 4;
        bat_r = colour_lib[2][0]; bat_g = colour_lib[2][1]; bat_b = colour_lib[2][2];
    } else {
        bat_end_led = 5;
        bat_r = colour_lib[3][0]; bat_g = colour_lib[3][1]; bat_b = colour_lib[3][2];
    }
    if (f_charging) {
        low_bat_blink_cnt = 6;
#if (CHARGING_SHIFT)
        bat_charging_design(bat_end_led, bat_r >> 2, bat_g >> 2, bat_b >> 2);
#else
        bat_charging_breathe();
#endif
    } else if (bat_percent < 10) {
        low_bat_show();
    } else {
        /* Steady state: all 5 LEDs same colour — matches NuPhy manual. */
        bat_end_led = 4;
        low_bat_blink_cnt = 6;
        for (i = 0; i <= bat_end_led; i++)
            side_status_led_set(i, bat_r, bat_g, bat_b);
    }
}

bool low_bat_flag = 0;
/**
 * @brief  battery state indicate
 */
void bat_led_show(void) {
    static bool     bat_show_flag    = true;
    static uint32_t bat_show_time    = 0;
    static uint32_t bat_sts_debounce = 0;
    static uint32_t bat_per_debounce = 0;
    static uint8_t  charge_state     = 0;
    static uint8_t  bat_percent      = 0;
    static bool     f_init           = 1;
    static uint8_t  prev_rf_state    = 0xff;

    if (dev_info.link_mode != LINK_USB) {
        if (rf_link_show_time < RF_LINK_SHOW_TIME) return;

        if (dev_info.rf_state != RF_CONNECT) {
            prev_rf_state = dev_info.rf_state;
            return;
        }

        if (prev_rf_state != RF_CONNECT) {
            f_init        = 1;
            bat_show_flag = true;
            f_charging    = false;
        }
        prev_rf_state = RF_CONNECT;
    }

    if (f_init) {
        f_init        = 0;
        bat_show_time = timer_read32();
        charge_state  = dev_info.rf_charge;
        bat_percent   = dev_info.rf_baterry;
    }

    if (charge_state != dev_info.rf_charge) {
        if (timer_elapsed32(bat_sts_debounce) > 1000) {
            if (((charge_state & 0x01) == 0) && ((dev_info.rf_charge & 0x01) != 0)) {
                bat_show_flag = true;
                f_charging    = true;
                bat_show_time = timer_read32();
            }
            charge_state = dev_info.rf_charge;
        }
    } else {
        bat_sts_debounce = timer_read32();
        if (f_charging) {
            if (timer_elapsed32(bat_show_time) > 10000) {
                bat_show_flag = false;
                f_charging    = false;
            }
        } else {
            if (timer_elapsed32(bat_show_time) > 5000) {
                bat_show_flag = false;
            }
        }
        if (charge_state == 0x03) {
            f_charging = true;
        } else if (!(charge_state & 0x01)) {
            f_charging = 0;
        }
    }

    if (bat_percent != dev_info.rf_baterry) {
        if (timer_elapsed32(bat_per_debounce) > 1000) {
            bat_percent = dev_info.rf_baterry;
        }
    } else {
        bat_per_debounce = timer_read32();

        if ((bat_percent < 10) && (!(charge_state & 0x01))) {
            bat_show_flag = true;
            bat_show_time = timer_read32();
            low_bat_flag  = 1;
            if (rgb_matrix_config.hsv.v > RGB_MATRIX_VAL_STEP) {
                rgb_matrix_config.hsv.v = RGB_MATRIX_VAL_STEP;
            }

            if (side_light > 1) {
                side_light = 1;
            }
        } else
            low_bat_flag = 0;
    }
    if (f_bat_hold || bat_show_flag) {
        bat_percent_led(bat_percent);
    }
}

void device_reset_init(void) {
    side_mode_a     = 0;
    side_mode_b     = 3;
    side_light      = 2;
    side_speed      = 2;
    side_rgb        = 1;
    side_colour     = 0;
    side_play_point = 0;

    side_play_cnt   = 0;
    side_play_timer = timer_read32();

    f_bat_hold = false;

    rgb_matrix_enable();
    rgb_matrix_mode(RGB_MATRIX_DEFAULT_MODE);
    rgb_matrix_set_speed(255 - RGB_MATRIX_SPD_STEP * 2);
    rgb_matrix_sethsv(RGB_DEFAULT_COLOUR, 255, RGB_MATRIX_MAXIMUM_BRIGHTNESS - RGB_MATRIX_VAL_STEP * 2);

    user_config.default_brightness_flag = 0xA6;
    user_config.ee_side_led            = side_led_pack(side_mode_a, side_mode_b, side_rgb, side_colour, side_light, side_speed);
    user_config.ee_debounce_press_ms    = 5;
    user_config.ee_debounce_release_ms  = 5;
    user_config.ee_sleep_timeout        = SLEEP_TIMEOUT_DEFAULT;
    set_f_dev_sleep_enable(true);
    set_f_usb_sleep_enable(false);
    set_f_deep_sleep_enable(true);
    set_nkro_mode(NKRO_AUTO);
    user_config_mark_dirty();
}

/**
 * @brief  rgb test
 */
void rgb_test_show(void) {
    gpio_write_pin_high(DC_BOOST_PIN);
    gpio_write_pin_high(RGB_DRIVER_SDB1);
    gpio_write_pin_high(RGB_DRIVER_SDB2);
    rgb_matrix_set_color_all(0xFF, 0x00, 0x00);
    rgb_matrix_update_pwm_buffers();
    wait_ms(1000);
    rgb_matrix_set_color_all(0x00, 0xFF, 0x00);
    rgb_matrix_update_pwm_buffers();
    wait_ms(1000);
    rgb_matrix_set_color_all(0x00, 0x00, 0xFF);
    rgb_matrix_update_pwm_buffers();
    wait_ms(1000);
}

#ifdef RGB_DEBUG
/**
 * @brief  Stream side/status LED changes over the console.
 *
 * Called every housekeeping loop but short-circuits unless the status LED
 * buffer or the battery/RF context actually changed — an O(5) compare with
 * zero output in steady state, so it never floods the console or stalls the
 * main loop.
 */
/* Debug harness state — declared early so rgb_debug_task can read it. */
#define RGB_DBG_BATTERY 0
#define RGB_DBG_LEDSTEP 1
#define RGB_DBG_PROG_COUNT 2

static uint8_t  rgb_dbg_program = RGB_DBG_BATTERY;
static uint8_t  rgb_dbg_bat     = 100;
static uint16_t rgb_dbg_led     = 0;

void rgb_debug_task(void) {
    static rgb_t   last_rgb[5]   = {0};
    static uint8_t last_dbg_bat  = 0xff;
    static uint16_t last_dbg_led = 0xffff;
    static uint8_t last_prog     = 0xff;

    bool changed = false;
    for (int i = 0; i < 5; i++) {
        if (side_status_rgb[i].r != last_rgb[i].r || side_status_rgb[i].g != last_rgb[i].g || side_status_rgb[i].b != last_rgb[i].b) {
            changed = true;
        }
    }
    if (rgb_dbg_program != last_prog || rgb_dbg_bat != last_dbg_bat || rgb_dbg_led != last_dbg_led) {
        changed = true;
    }
    if (!changed) return;

    for (int i = 0; i < 5; i++)
        last_rgb[i] = side_status_rgb[i];
    last_prog     = rgb_dbg_program;
    last_dbg_bat  = rgb_dbg_bat;
    last_dbg_led  = rgb_dbg_led;

    if (rgb_dbg_program == RGB_DBG_BATTERY) {
        uprintf("DBG prog=BATTERY sim_bat=%d%% status=[", rgb_dbg_bat);
    } else {
        uprintf("DBG prog=LEDSTEP led=%d/%d status=[", rgb_dbg_led, RGB_MATRIX_LED_COUNT - 1);
    }
    for (int i = 0; i < 5; i++)
        uprintf("%02X%02X%02X%s", side_status_rgb[i].r, side_status_rgb[i].g, side_status_rgb[i].b, i < 4 ? " " : "");
    uprintf("]\n");
}

/* ---------------------------------------------------------------------------
 * RGB_DEBUG test harness
 *
 * Two interactive test "programs", selected with FN+Left / FN+Right:
 *   0 BATTERY : simulate the battery display. FN+, / FN+. step the simulated
 *               percentage in 10%% increments so every segment/colour state
 *               can be inspected without draining a real battery.
 *   1 LEDSTEP : light one matrix LED at a time. FN+, / FN+. walk the index
 *               across all RGB_MATRIX_LED_COUNT LEDs to map the physical
 *               layout.
 * All normal effects/side shows are masked while this is active (see
 * rgb_matrix_indicators_advanced_user).
 * ------------------------------------------------------------------------- */
static void rgb_debug_report(void) {
    if (rgb_dbg_program == RGB_DBG_BATTERY) {
        uprintf("DBG prog=BATTERY sim_bat=%d%%\n", rgb_dbg_bat);
    } else {
        uprintf("DBG prog=LEDSTEP led=%d/%d\n", rgb_dbg_led, RGB_MATRIX_LED_COUNT - 1);
    }
}

void rgb_debug_cycle_program(int8_t dir) {
    rgb_dbg_program = (rgb_dbg_program + RGB_DBG_PROG_COUNT + (dir > 0 ? 1 : -1)) % RGB_DBG_PROG_COUNT;
    rgb_debug_report();
}

void rgb_debug_step_value(int8_t dir) {
    if (rgb_dbg_program == RGB_DBG_BATTERY) {
        if (dir > 0)
            rgb_dbg_bat = (rgb_dbg_bat >= 100) ? 100 : rgb_dbg_bat + 10;
        else
            rgb_dbg_bat = (rgb_dbg_bat <= 10) ? 0 : rgb_dbg_bat - 10;
    } else {
        if (dir > 0)
            rgb_dbg_led = (rgb_dbg_led + 1) % RGB_MATRIX_LED_COUNT;
        else
            rgb_dbg_led = (rgb_dbg_led + RGB_MATRIX_LED_COUNT - 1) % RGB_MATRIX_LED_COUNT;
    }
    rgb_debug_report();
}

void rgb_debug_render(void) {
    rgb_matrix_set_color_all(0, 0, 0);

    if (rgb_dbg_program == RGB_DBG_BATTERY) {
        // Force the static bar (not the charging breathe) so the simulated
        // percentage maps directly onto the segment/colour output.
        bool save_chg = f_charging;
        f_charging    = false;
        bat_percent_led(rgb_dbg_bat);
        f_charging = save_chg;
    } else {
        rgb_matrix_set_color(rgb_dbg_led, 0xff, 0xff, 0xff);
    }
}
#endif

/**
 * @brief  side_led_show.
 */
void m_side_led_show(void) {
    static bool flag_power_on = 1;

    side_play_cnt += timer_elapsed32(side_play_timer);
    side_play_timer = timer_read32();

    if (flag_power_on) {
        if (!f_dial_sw_init_ok) return;
        flag_power_on = 0;
    }

    // Throttle the full side LED render (animation + status LEDs) to
    // ~60 fps.  side_play_cnt is already accumulated above so animation
    // timing stays accurate even when we skip the render this call.
    static uint32_t side_last_refresh = 0;
    if (timer_elapsed32(side_last_refresh) < SIDE_REFRESH_INTERVAL) {
        return;
    }
    side_last_refresh = timer_read32();

    if (f_power_show) {
        side_power_mode_show();
        return;
    }

    if (rgb_matrix_get_mode() == RGB_MATRIX_CUSTOM_lesbian_pride) {
        side_line   = SIDE_LED_COUNT;
        f_side_flag = 0x1f;
        side_pride_rainbow_show();
        bat_led_show();
        sys_led_show();
        sys_sw_led_show();
        sleep_sw_led_show();
        rf_led_show();
        return;
    }

    switch (side_mode_b) {
        case SIDE_MODE_1:
            side_line   = 0;
            f_side_flag = 0;
            break;

        case SIDE_MODE_2:
            side_line   = SIDE_LED_COUNT;
            f_side_flag = 0x08;
            break;

        case SIDE_MODE_3:
            side_line   = SIDE_LED_COUNT;
            f_side_flag = 0x18;
            break;

        case SIDE_MODE_4:
            side_line   = SIDE_LED_COUNT;
            f_side_flag = 0x1f;
            break;

        case SIDE_MODE_5:
            side_line   = SIDE_LED_COUNT;
            f_side_flag = 0x01;
            break;

        case SIDE_MODE_6:
            side_line   = SIDE_LED_COUNT;
            f_side_flag = 0x09;
            break;

        case SIDE_MODE_7:
            side_line   = SIDE_LED_COUNT;
            f_side_flag = 0x19;
            break;
        default:
            break;
    }

    switch (side_mode_a) {
        case SIDE_WAVE:
            side_wave_mode_show();
            break;
        case SIDE_NEW:
            side_new_mode_show();
            break;
        case SIDE_MIX:
            side_spectrum_mode_show();
            break;
        case SIDE_BREATH:
            side_breathe_mode_show();
            break;
        case SIDE_STATIC:
            side_static_mode_show();
            break;
    }

    bat_led_show();
    sys_led_show();
    sys_sw_led_show();
    sleep_sw_led_show();
    rf_led_show();
}
