# Myceliatrix QMK Firmware Fork

This is a personal fork of the QMK firmware, focused on adding and improving support for NuPhy keyboards. It is based on both the [NuPhy upstream QMK fork](https://github.com/nuphy-src/qmk_firmware) and the [official QMK firmware](https://github.com/qmk/qmk_firmware).

## What This Is

This repository contains a customized QMK firmware targeting NuPhy keyboards. It exists primarily as a playground for experimentation, feature additions, and fixes that may not yet be present (or may never be merged) into upstream repositories.

## Problems It Solves

- Provides an open, hackable firmware alternative for NuPhy keyboards.
- Enables custom keymaps, macros, and QMK-specific features (e.g., Tap Dance, Combos, Auto Shift) on supported hardware.
- Serves as a staging area for patches and tweaks before potentially upstreaming them.

## Supported Keyboards

The following NuPhy model is actively supported, tested, and provided with builds by this fork:

- [Halo75 V2](/keyboards/nuphy/halo75_v2/) — Both ANSI and ISO variants

### Halo75 V2 Improvements

The Halo75 V2 firmware in this fork includes several improvements over the upstream NuPhy firmware:

- **Status LED Protection**: Built-in RGB matrix effects no longer interfere with status LEDs (83-87), ensuring system status indicators remain visible
- **Custom RGB Effects**: `game_mode` and `position_mode` effects properly isolated to main keyboard LEDs only
- **Better RGB Matrix Handling**: All built-in QMK RGB matrix effects work correctly without affecting status LEDs

## Disclaimer

> **WARNING:** I do **not** recommend flashing this firmware onto your keyboard unless you know exactly what you are doing. This is a personal, experimental fork and may contain bugs, incomplete features, or changes that could cause your keyboard to behave unexpectedly or become unresponsive. Flashing firmware always carries the risk of bricking your device. Use at your own risk.

## Upstream Credits

This project would not exist without the upstream projects it is derived from:

- [NuPhy QMK Firmware](https://github.com/nuphy-src/qmk_firmware) — the NuPhy-specific QMK source this fork is based on.
- [QMK Firmware](https://github.com/qmk/qmk_firmware) — the original and official Quantum Mechanical Keyboard firmware.

All credit for the underlying QMK ecosystem goes to the QMK maintainers and contributors, and all credit for the NuPhy-specific additions goes to NuPhy and their contributors.
