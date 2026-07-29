SRC += side.c rf.c sleep.c rf_driver.c mcu_pwr.c matrix.c debounce.c socd.c rgb_matrix_driver.c
CUSTOM_MATRIX = lite
CAPS_WORD_ENABLE      = yes
UART_DRIVER_REQUIRED   = yes
RGB_MATRIX_CUSTOM_KB   = yes

# The stock IS31FL3733 chip implementation is reused unchanged. Only the
# two-driver flush schedule is keyboard-local so Halo matrix capture can run
# between transfers without changing QMK's shared LED driver.
I2C_DRIVER_REQUIRED    = yes
VPATH                 += $(DRIVER_PATH)/led/issi
SRC                   += $(DRIVER_PATH)/led/issi/is31fl3733.c

# RGB_DEBUG=yes: instead of dumping HID/matrix events, stream side/status
# LED buffer changes (battery, RF, sys, sleep). QMK's rules parser does not
# evaluate conditionals when generating feature metadata, so CONSOLE_ENABLE
# must remain an explicit build override rather than being assigned here.
# Usage: qmk flash -kb nuphy/halo75_v2/iso -km via \
#          -e CONSOLE_ENABLE=yes -e RGB_DEBUG=yes
ifeq ($(strip $(RGB_DEBUG)), yes)
    OPT_DEFS += -DRGB_DEBUG
endif
