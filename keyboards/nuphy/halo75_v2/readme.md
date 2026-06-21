# NuPhy Halo75 V2

A 75% wireless mechanical keyboard with RGB matrix and side LEDs.

## Variants

- **ANSI** - Standard US layout
- **ISO** - ISO Nordic layout

## Features

- 75% layout with 82 keys
- RGB matrix with 128 LEDs (main keyboard + side LEDs)
- Side LED strip with status indicators and rim lighting
- Wireless connectivity via 2.4GHz receiver
- VIA configurator support
- Custom RGB matrix effects

## Building

### ANSI

```bash
qmk compile -kb nuphy/halo75_v2/ansi -km via
```

### ISO

```bash
qmk compile -kb nuphy/halo75_v2/iso -km via
```

## Flashing

### ANSI

```bash
qmk flash -kb nuphy/halo75_v2/ansi -km via
```

### ISO

```bash
qmk flash -kb nuphy/halo75_v2/iso -km via
```

## LED Matrix

The keyboard has 128 LEDs controlled by two IS31FL3733 drivers:
- **Main keyboard LEDs**: 0-82
- **Status LEDs**: 83-87 (system status, battery indicator)
- **Rim LEDs**: 88-126 (side lighting strip)

See the variant-specific readme for detailed LED matrix mappings:
- [ANSI LED Matrix](./ansi/readme.md)
- [ISO LED Matrix](./iso/readme.md)

## Custom RGB Effects

This firmware includes custom RGB matrix effects:
- `game_mode` - Game-focused lighting effect
- `position_mode` - Position-based lighting effect

These effects are designed to only control the main keyboard LEDs (0-82), leaving the side LEDs (83-127) to the side LED system.

## Status LED Protection

The firmware includes protection for status LEDs (83-87) to prevent built-in RGB matrix effects from interfering with system status indicators. Status LEDs are automatically restored after each frame to maintain proper system feedback.

## Debug Modes

The firmware supports two console-enabled debug builds. Both require a USB connection — the console is not available over wireless.

### HID / Matrix Console Debug

Standard QMK console debug that logs matrix scan changes, key events, and HID report data. Useful for diagnosing ghosting, key chatter, or protocol issues.

```bash
qmk flash -kb nuphy/halo75_v2/iso -km via -e CONSOLE_ENABLE=yes
```

Enables:
- `debug_matrix` — raw and debounced matrix row dumps on every change
- `debug_keyboard` — key event logs (keycode, row/col, layer, mods)
- `debug_mouse` — mouse report logs (if `MOUSEKEY_ENABLE`)

Output is high-volume. Use `qmk console` to view:

```bash
qmk console
```

Sample output:

```
DBG NuPhy Halo75 V2 console enabled
rows=12 cols=16 diode=COL2ROW default_layer=1 layer_state=00000002
RAW 00000000 00000000 00000002 ...
DBN 00000000 00000000 00000002 ...
EV k=7004 r=0 c=1 down layer=1 mods=00 weak=00 oneshot=00 row=00000002
```

### RGB Debug Mode

A special build that mutes all normal LED effects and provides two interactive test programs for isolating and verifying the status LED display and individual LED mapping. All side animations, battery/RF/sleep LED shows, and RGB matrix effects are dead-stripped from the binary.

```bash
qmk flash -kb nuphy/halo75_v2/iso -km via -e RGB_DEBUG=yes
```

This enables the console (same as `CONSOLE_ENABLE=yes`) but silences HID/matrix event logging so the LED update stream is readable.

#### Debug Programs

Two programs are available, switchable at runtime:

| Program | Name | Description |
|---------|------|-------------|
| 0 | BATTERY | Simulates the 5-segment battery bar graph at a configurable percentage. Shows the actual RGB values written to all 5 status LEDs on the console. |
| 1 | LEDSTEP | Lights a single matrix LED white at a time. Steps through all 128 LEDs to verify physical LED-to-index mapping. |

#### Controls

The debug harness repurposes existing FN-layer keycodes:

| Key Combo | Keycode | Debug Action |
|-----------|---------|-------------|
| FN + Left | `RM_NEXT` | Switch to previous debug program |
| FN + Right | `RM_HUEU` | Switch to next debug program |
| FN + Comma | `RM_SPDD` | Decrease value (battery % in 10% steps, or LED index by 1) |
| FN + Dot | `RM_SPDU` | Increase value (battery % in 10% steps, or LED index by 1) |

These keycodes are intercepted in `process_record_kb` and swallowed (return `false`), so they have no normal RGB matrix side effects during debug mode.

#### Console Output

The console prints a line only when the status LED buffer or debug state changes — no flooding in steady state:

```
DBG prog=BATTERY sim_bat=80% status=[FFFF00 FFFF00 FFFF00 000000 000000]
DBG prog=BATTERY sim_bat=70% status=[FF8C00 FF8C00 FF8C00 000000 000000]
DBG prog=LEDSTEP led=42/127 status=[000000 000000 000000 000000 000000]
```

The `status=[...]` field shows the hex RGB values (RRGGBB) of all 5 status LEDs (indices 83–87), so you can verify exactly which segments are lit and what colour each one is.

#### Battery Bar Graph Colour Palette

The BATTERY program uses the same colour mapping as the real battery display:

| Battery Range | Segments Lit | Colour | Hex |
|---------------|-------------|-------|-----|
| 0–20% | 1 | Red | `FF0000` |
| 21–40% | 2 | Orange-Red | `FF4500` |
| 41–60% | 3 | Orange | `FF8C00` |
| 61–80% | 4 | Yellow | `FFFF00` |
| 81–100% | 5 | Green | `00FF00` |

#### Implementation Files

| File | Role |
|------|------|
| `rules.mk` | `RGB_DEBUG` build flag, enables console + `-DRGB_DEBUG` |
| `halo75_v2.c` | Console init, keycode interception in `process_record_kb`, debug render in `rgb_matrix_indicators_advanced_user`, `rgb_debug_task` call in `housekeeping_task_kb` |
| `side.c` | `rgb_debug_task` (console logger), `rgb_debug_render` (program renderer), `rgb_debug_cycle_program` / `rgb_debug_step_value` (step controls) |

## Keymaps

- **via** - VIA configurator compatible keymap
- **default** - Default keymap

## Notes

- This is a community-maintained fork of the NuPhy firmware
- Fixes include proper status LED protection and improved RGB matrix effect handling
- Wireless features require the 2.4GHz receiver
