SRC += side.c rf.c sleep.c rf_driver.c mcu_pwr.c matrix.c
CUSTOM_MATRIX = lite
UART_DRIVER_REQUIRED   = yes
RGB_MATRIX_CUSTOM_KB   = yes

# RGB_DEBUG=yes: enable the USB console but, instead of dumping HID/matrix
# events, stream side/status LED buffer changes (battery, RF, sys, sleep).
# Usage: qmk flash -kb nuphy/halo75_v2/iso -km via -e RGB_DEBUG=yes
ifeq ($(strip $(RGB_DEBUG)), yes)
    CONSOLE_ENABLE = yes
    OPT_DEFS += -DRGB_DEBUG
endif
