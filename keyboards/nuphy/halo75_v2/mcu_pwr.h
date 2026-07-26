/*
Copyright 2023 NuPhy & jincao1

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

#include <stdbool.h>

/*
 * EXTI port/pin mappings for the Halo75 V2 matrix rows.
 * Rows: C14, C15, A0, A1, A2, A3
 * During deep sleep, columns are driven HIGH and rows are set as
 * input-low.  Any keypress pulls a row LOW, triggering a falling-edge
 * EXTI interrupt that wakes the MCU from STOP mode.
 *
 * EXTI_PortSourceGPIOx values match the STM32F0 SYSCFG_EXTICR encoding:
 *   GPIOA = 0, GPIOB = 1, GPIOC = 2, ...
 */
#define EXTI_PORT_R0 2 /* GPIOC */ // C14
#define EXTI_PORT_R1 2 /* GPIOC */ // C15
#define EXTI_PORT_R2 0 /* GPIOA */ // A0
#define EXTI_PORT_R3 0 /* GPIOA */ // A1
#define EXTI_PORT_R4 0 /* GPIOA */ // A2
#define EXTI_PORT_R5 0 /* GPIOA */ // A3

#define EXTI_PIN_R0 14 /* C14 */
#define EXTI_PIN_R1 15 /* C15 */
#define EXTI_PIN_R2 0  /* A0  */
#define EXTI_PIN_R3 1  /* A1  */
#define EXTI_PIN_R4 2  /* A2  */
#define EXTI_PIN_R5 3  /* A3  */

void enter_light_sleep(void);
void exit_light_sleep(void);
void prepare_deep_sleep(void);
void enter_deep_sleep(void);
void exit_deep_sleep(void);

void pwr_rgb_led_off(void);
void pwr_rgb_led_on(void);

bool is_rgb_led_on(void);

void led_pwr_sleep_handle(void);
void led_pwr_wake_handle(void);
