# QMK Firmware — NuPhy Halo75 V2 Fork

This is a fork of [NuPhy's official QMK firmware](https://github.com/nuphy-src/qmk_firmware) with modern QMK pulled in from [QMK's own repository](https://github.com/qmk/qmk_firmware). It focuses on keeping the **NuPhy Halo75 V2** keyboard fully functional with the latest QMK, including proper ISO support, wireless connectivity, and side LED control.

## Why This Exists

NuPhy does not publish ISO firmware for the Halo75 V2. The only firmware available on the [NuPhy homepage](https://nuphy.com/) is ANSI, and NuPhy support was unable to provide an ISO build when asked. Flashing the ANSI firmware onto an ISO keyboard leaves several keys broken — the `<>` key next to left shift, the `§` key next to ESC, the ISO Enter, and the `'` key all produce wrong characters.

Rather than wait for a firmware that may never come, this fork was created to fix it properly. What started as "just get ISO keys working" turned into a full modernization: the original NuPhy QMK port was several major QMK versions behind and had multiple critical bugs beyond the ISO layout issue.

## What's Fixed

### ISO Layout Support
- Correct ISO keymap with `KC_NUBS`, `KC_NUHS`, and proper matrix positions for the `<>`, `§`, `'`, and ISO Enter keys
- Complete Fn layer with LED control keycodes (brightness, hue, speed, mode)
- Fn+M layer for side LED control (separate from the RGB matrix effects)
- VIA configurator support with a layout definition JSON for usevia.app

### Wireless Connectivity (USB / Bluetooth / 2.4GHz RF)
The original port had two separate bugs that completely broke wireless:
- **UART pin conflict**: The QMK UART driver defaulted to pins A9/A10, which are also matrix columns 13/14. When the RF UART initialized, it reconfigured those columns as UART pins, causing phantom key presses (ghosting). Fixed by explicitly setting UART to PB6/PB7.
- **Wrong alternate function mode**: The STM32F072 (Cortex-M0) uses AF0 for USART1 on PB6/PB7, not AF7 (which is correct for STM32F4 but maps to comparator outputs on STM32F0). The UART peripheral was never actually connected to the physical pins, so the nRF module never received any commands. Fixed by setting `UART_TX_PAL_MODE = 0` and `UART_RX_PAL_MODE = 0`.
- **USB suspend check**: The `no_startup_check` flag (maps to `NO_USB_STARTUP_CHECK`) is required because in RF/BT mode there is no USB connection. Without it, QMK sees the USB driver as permanently suspended and enters a power-down loop, blocking all wireless communication.

### RGB Matrix and Side LEDs
- IS31FL3733 LED driver configuration fixed for modern QMK (I2C addresses, pull-up/down resistor macros, LED array format)
- Side LED flickering fixed: the side LED system and RGB matrix effects were both writing to LEDs 83-127 every frame, causing them to alternate colors. Fixed by moving side LED rendering into `rgb_matrix_indicators_advanced_user()` so it runs after the effect but before the PWM flush.
- Modern RGB matrix keycodes (`RM_VALU`, `RM_NEXT`, `RM_HUEU`, etc.) replace deprecated `RGB_*` keycodes
- Custom effects: `game_mode` (lights only WASD + arrows) and `position_mode` (lights only F, J, and up arrow for touch-typing reference)

### Code Cleanup
- Updated all GPIO API calls to modern `gpio_*` functions
- Updated eeconfig API to 3-argument datablock functions
- Removed dead code (unreachable RGB test path, unused variables, debug toggles)
- Disabled console feature (saves ~4KB flash, was only needed for debug prints)
- Both ISO and ANSI variants fully modernized and building clean

## Supported Keyboards

| Keyboard | Layout | Status |
|---|---|---|
| NuPhy Halo75 V2 | ISO (Nordic) | Working — primary development target |
| NuPhy Halo75 V2 | ANSI | Working — modernized alongside ISO |

Only the Halo75 V2 is supported. No other NuPhy keyboards are included because I don't own them and can't test them. If you want to add support for another NuPhy board, the fixes documented in the git history (especially the UART pin/PAL mode and `no_startup_check` issues) are likely applicable to other NuPhy wireless keyboards using the STM32F072.

## Building

```bash
# Install QMK CLI if you haven't already
python3 -m pip install qmk

# Build ISO with VIA support
qmk compile -kb nuphy/halo75_v2/iso -km via

# Build ISO default keymap
qmk compile -kb nuphy/halo75_v2/iso -km default

# Build ANSI with VIA support
qmk compile -kb nuphy/halo75_v2/ansi -km via

# Build ANSI default keymap
qmk compile -kb nuphy/halo75_v2/ansi -km default
```

Flash with:
```bash
qmk flash -kb nuphy/halo75_v2/iso -km via
```

Or put the keyboard into DFU mode (hold ESC while plugging in) and:
```bash
dfu-util -a 0 -d 1915:32F5 -D nuphy_halo75_v2_iso_via.bin
```

## Downloads

Pre-built firmware is available from the [Forgejo releases page](https://code.augustini.xyz/myceliatrix/qmk_firmware/releases). Each release includes:

- `nuphy_halo75_v2_iso_via.bin` / `nuphy_halo75_v2_iso_default.bin`
- `nuphy_halo75_v2_ansi_via.bin` / `nuphy_halo75_v2_ansi_default.bin`
- `nuphy_halo75_v2_iso_via.json` / `nuphy_halo75_v2_ansi_via.json` (VIA layout definitions for usevia.app)
- `SHA256SUMS` (checksums for all artifacts)

Nightly builds are also published automatically via Woodpecker CI.

## VIA Configuration

To use the [VIA configurator](https://usevia.app/), load the appropriate VIA JSON file (`nuphy_halo75_v2_iso_via.json` or `nuphy_halo75_v2_ansi_via.json`) using the "Design" tab, then switch to the "Configure" tab to remap keys, configure layers, and set up macros.

## Contact

- **Matrix:** [@myceliatrix:augustini.wtf](https://matrix.to/#/@myceliatrix:augustini.wtf)
- **Mastodon:** [@myceliatrix@social.augustini.wtf](https://social.augustini.wtf/@myceliatrix)

## Upstream

This fork is based on [NuPhy's official QMK firmware](https://github.com/nuphy-src/qmk_firmware) with updates merged in from [QMK Firmware](https://github.com/qmk/qmk_firmware) upstream. The QMK documentation is available at [docs.qmk.fm](https://docs.qmk.fm).
