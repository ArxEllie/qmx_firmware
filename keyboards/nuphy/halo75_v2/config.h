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

#define RGB_MATRIX_LED_FLUSH_LIMIT 32

#define TAP_CODE_DELAY 8
#define DYNAMIC_KEYMAP_MACRO_DELAY 8
// This is the size of the EEPROM for the custom VIA-specific data
#define EECONFIG_USER_DATA_SIZE 12

#define DEV_MODE_PIN C0
#define SYS_MODE_PIN C1
#define DC_BOOST_PIN C2
#define NRF_RESET_PIN B4
#define NRF_BOOT_PIN B5
#define NRF_WAKEUP_PIN C4

#define RGB_DRIVER_SDB1 C6
#define RGB_DRIVER_SDB2 C7

#define SERIAL_DRIVER SD1
#define SD1_TX_PIN B6
#define SD1_TX_PAL_MODE 0
#define SD1_RX_PIN B7
#define SD1_RX_PAL_MODE 0

// QMK's uart_serial.c driver uses UART_TX_PIN/UART_RX_PIN (not SD1_*).
// Without these overrides it defaults to A9/A10 — which are matrix
// columns 13/14 — causing phantom key presses when rf_uart_init() runs.
#define UART_TX_PIN B6
#define UART_RX_PIN B7

// On STM32F072 (Cortex-M0), USART1 on PB6/PB7 is AF0, not AF7.
// The QMK uart driver defaults UART_TX/RX_PAL_MODE to 7 (correct for
// STM32F4, wrong for STM32F0 where AF7 = comparator outputs).
// Without these overrides the USART1 peripheral never connects to the
// physical pins and the nRF module receives nothing.
#define UART_TX_PAL_MODE 0
#define UART_RX_PAL_MODE 0

// This is a 7-bit address, that gets left-shifted and bit 0
// set to 0 for write, 1 for read (as per I2C protocol)
// The address will vary depending on your wiring:
// 0b1110100 AD <-> GND
// 0b1110111 AD <-> VCC
// 0b1110101 AD <-> SCL
// 0b1110110 AD <-> SDA
#define DRIVER_ADDR_1 0b1010000
#define DRIVER_ADDR_2 0b1010011

#define ISSI_TIMEOUT 1

/* I2C Alternate function settings */
#define I2C_DRIVER I2CD1
#define I2C1_SCL_PIN B8
#define I2C1_SDA_PIN B9

#define I2C1_SCL_PAL_MODE 1
#define I2C1_SDA_PAL_MODE 1

/*
 * 1 MHz Fast-mode Plus for a 48 MHz I2C kernel clock, analog filter enabled,
 * and digital filter disabled.  These fields encode TIMINGR=0x00500A13 and
 * come from ST's STM32F0 timing equations (60 ns rise, 100 ns fall).  Keeping
 * the individual fields makes QMK's I2Cv2 configuration explicit.
 */
#define I2C1_TIMINGR_PRESC 0U
#define I2C1_TIMINGR_SCLDEL 5U
#define I2C1_TIMINGR_SDADEL 0U
#define I2C1_TIMINGR_SCLH 10U
#define I2C1_TIMINGR_SCLL 19U

#define DRIVER_COUNT 2
#define DRIVER_1_LED_TOTAL 64
#define DRIVER_2_LED_TOTAL 64
#define RGB_MATRIX_LED_COUNT (DRIVER_1_LED_TOTAL + DRIVER_2_LED_TOTAL)

/* RGB_MATRIX_DRIVER=custom deliberately bypasses the stock driver's feature
 * define, so provide the one size declaration its unchanged chip code needs. */
#define IS31FL3733_LED_COUNT RGB_MATRIX_LED_COUNT

#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_CUSTOM_position_mode
#define RGB_DEFAULT_COLOUR 168

#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES
#define RGB_MATRIX_KEYRELEASES
/* QMK's suspend hooks are no-ops without this feature. Render off once and
 * stop the RGB task while the Halo's LED rail/drivers are powered down, so an
 * idle keyboard cannot continuously issue I2C transfers to sleeping devices. */
#define RGB_MATRIX_SLEEP

#define IS31FL3733_SW_PULLUP PUR_05KR
#define IS31FL3733_CS_PULLDOWN PUR_05KR

#define DEBOUNCE_DEFAULT_MS 5
#define DEBOUNCE_MIN_MS 1
#define DEBOUNCE_MAX_MS 99
#define DEBOUNCE_STEP 1

/* Default SOCD mode on first boot / factory reset.
 * 0=off, 1=neutral, 2=last-wins, 3=first-wins */
#define SOCD_DEFAULT_MODE 0

/* Bump when custom VIA values change so VIA Configurator can detect
 * a compatible firmware. */
#define VIA_FIRMWARE_VERSION 0x00000004
