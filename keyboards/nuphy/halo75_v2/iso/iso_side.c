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
#include "iso.h"

const uint8_t side_led_index_tab[44] = {
    SIDE_INDEX + 0, // Status LED 1 (83)
    SIDE_INDEX + 1, // Status LED 2 (84)
    SIDE_INDEX + 2, // Status LED 3 (85)
    SIDE_INDEX + 3, // Status LED 4 (86)
    SIDE_INDEX + 4, // Status LED 5 (87)
    SIDE_INDEX + 5, // Rim LED start (88)
    SIDE_INDEX + 6,  SIDE_INDEX + 7, SIDE_INDEX + 8, SIDE_INDEX + 9, SIDE_INDEX + 10, SIDE_INDEX + 11, SIDE_INDEX + 12, SIDE_INDEX + 13, SIDE_INDEX + 14, SIDE_INDEX + 15, SIDE_INDEX + 16, SIDE_INDEX + 17, SIDE_INDEX + 18, SIDE_INDEX + 19, SIDE_INDEX + 20, SIDE_INDEX + 21, SIDE_INDEX + 22, SIDE_INDEX + 23, SIDE_INDEX + 24, SIDE_INDEX + 25, SIDE_INDEX + 26, SIDE_INDEX + 27, SIDE_INDEX + 28, SIDE_INDEX + 29, SIDE_INDEX + 30, SIDE_INDEX + 31, SIDE_INDEX + 32, SIDE_INDEX + 33, SIDE_INDEX + 34, SIDE_INDEX + 35, SIDE_INDEX + 36, SIDE_INDEX + 37, SIDE_INDEX + 38, SIDE_INDEX + 39, SIDE_INDEX + 40, SIDE_INDEX + 41, SIDE_INDEX + 42,
    SIDE_INDEX + 43, // Rim LED end (126)
};

uint8_t is_side_rgb_on(uint8_t index) {
    // Status LEDs (indices 0-4) are never part of rim animations.
    if (index < 5) return false;

    // ISO keeps the same SIDE_MOD_B bit groups as ANSI, adapted to its
    // rim-only range after the five status LEDs at the head of the table.
    if (((index >= 5) && (index <= 15)) && (f_side_flag & 0x01))
        return true;
    else if ((((index >= 16) && (index <= 22)) || ((index >= 28) && (index <= 34))) && (f_side_flag & 0x02))
        return true;
    else if (((index >= 40) && (index <= 43)) && (f_side_flag & 0x04))
        return true;
    else if (((index >= 23) && (index <= 27)) && (f_side_flag & 0x08))
        return true;
    else if (((index >= 35) && (index <= 36)) && (f_side_flag & 0x10))
        return true;
    else if (((index >= 37) && (index <= 39)) && (f_side_flag & 0x01))
        return true;
    else
        return false;
}
