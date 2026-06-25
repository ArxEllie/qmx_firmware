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
#pragma once

#define STARRY_INDEX_LEN (160)
#define WAVE_TAB_LEN 112
#define BREATHE_TAB_LEN 128
#define MIXCOLOUR_TAB_LEN 144
#define FLOW_COLOUR_TAB_LEN 192
#define FIREWORK_INDEX_LEN (158)
#define STARRY_DATA_LEN 96
#define TIDE_DATA_LEN 120

extern const uint8_t light_value_tab[101];
extern const uint8_t breathe_data_tab[BREATHE_TAB_LEN];
extern const uint8_t wave_data_tab[WAVE_TAB_LEN];

extern const uint8_t flow_rainbow_colour_tab[FLOW_COLOUR_TAB_LEN][3];
extern const uint8_t dual_colour_lib[3][6];
extern const uint8_t colour_lib[9][3];
extern const uint8_t colour_lib_1[9][3];

