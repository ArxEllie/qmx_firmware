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
- `SCAN` — five-second scan-latency and custom matrix-settle summary

Output is high-volume. Use `qmk console` to view:

```bash
qmk console
```

Sample output:

```
DBG NuPhy Halo75 V2 console enabled
rows=6 cols=17 diode=COL2ROW default_layer=1 layer_state=00000002
RAW 00000000 00000000 00000002 ...
DBN 00000000 00000000 00000002 ...
EV k=7004 r=0 c=1 down layer=1 mods=00 weak=00 oneshot=00 no_gui=0 sys=A2 row=00000002
SCAN max_gap=5 ms gaps_ge_4ms=156/5s settle_max=2 settle_caps=0 report_clears=0 clear_restores=0 gui_repairs=0 modifier_prepasses=0 wake=0 queued=0 replayed=0 restored=0 overflows=0
```

`max_gap` is the longest interval between physical matrix acquisitions,
including the capture inserted between the two RGB-driver transfers.
`settle_max` is the largest number of GPIO reads needed for a column line to
return high, and `settle_caps` counts rows that reached the 1,000-iteration
safety limit. A large `max_gap` with `settle_max=2` points to work outside the
matrix scanner, such as RF UART transmission or a stalled peripheral. The
normal ~4 ms two-driver RGB flush is split by a capture scan, so it should no
longer create a 4 ms blind interval. Any non-zero `settle_caps` implicates the
scanner, switch matrix, or electrical settling directly. After the initial
startup report, `report_clears` should stay at zero during ordinary typing; an
increment means a mode, sleep, link, or reset path deliberately erased all
pressed keys. `clear_restores` counts physically held basic HID keys rebuilt
after such a clear. Modifiers are rebuilt before ordinary keys, and a real
matrix edge cancels the corresponding pending restoration. This keeps a
legitimate transport/mode report clear from leaving Cmd—or any held basic
key—logically absent until the next physical press.

The RGB bus uses STM32F0 Fast-mode Plus timing generated from the 48 MHz system
clock (`TIMINGR=0x00500A13`). The previous nominal 1 MHz configuration selected
the 8 MHz HSI but programmed every timing field to zero; QMK's
`I2C1_CLOCK_SPEED` does not configure this I2Cv2 peripheral. That combination
could not satisfy the STM32F0 1 MHz timing constraints and made LED-transfer
latency and reliability dependent on out-of-spec bus timing.
The Halo uses QMK's custom RGB Matrix driver seam to keep its two-driver flush
schedule local to this keyboard. The underlying IS31FL3733 implementation
remains the unmodified shared QMK driver.
`RGB_MATRIX_SLEEP` is enabled and the custom light/deep-sleep power handlers
use the same suspend state. QMK renders one off frame before LED power is
removed, then stops producing I2C frames until wake. This prevents an idle
keyboard from repeatedly talking to drivers that are shut down or unpowered.
The two IS31FL3733 SDB pins are actively driven low during that transition,
matching NuPhy's original firmware; they are no longer left as floating GPIO
inputs whose shutdown level depends on unverified external pulls.
Host-initiated USB resume also exits the Halo's custom light-sleep state, so
QMK cannot restart RGB rendering while the driver power rail and SDB pins
remain off waiting for a physical wake key. It cancels both pending sleep
flags, preventing a sleep request scheduled immediately before resume from
putting the active keyboard straight back to sleep.
The first wake key requests the STM32 USB remote-wakeup pulse asynchronously.
ChibiOS' stock helper sleeps the calling thread for the 2 ms pulse duration;
the Halo instead uses a ChibiOS virtual timer to end the same pulse without
blocking matrix processing. USB resume ownership stays in the wake-event
queue, while the light/deep-sleep power helpers only restore local hardware.

For the physical Mac position, `no_gui` must always remain `0` and `sys` should
settle at `A2`. A non-zero `no_gui` would mean QMK is deliberately suppressing
Command before HID report generation. `gui_repairs` counts times the keyboard
found and corrected that invalid Mac-mode state before QMK handled a key.
`modifier_prepasses` counts physical modifier presses dispatched before QMK's
normal row-order walk. It increases for every basic modifier press. The Halo's
letter rows precede its bottom modifier row, so this modifier-first dispatch
prevents a same-scan Cmd+C from reaching the host as C followed by Cmd.

During a USB resume, `wake` counts resume event groups and `queued`/`replayed`
should match after the bus becomes active. `overflows` must remain zero; a
non-zero value means more than 32 transitions arrived before USB resumed.
`restored` counts keys that remained physically held across QMK's wake cleanup
and therefore needed their logical action state recreated without a new edge.
Replay waits for both the ChibiOS driver and QMK's logical USB state to become
active/configured. The latter changes only after `suspend_wakeup_init()` has
finished clearing stale keyboard state, preventing a late wake handler from
erasing an already-replayed Command press. Host-initiated resumes run the same
unchanged-hold restoration even when no key transition was queued. Restoration
intersects the current matrix with QMK's previous matrix snapshot, so a brand
new key in the first post-resume scan remains exclusively owned by the normal
event path and cannot be dispatched twice.

### RGB Debug Mode

A special build that mutes all normal LED effects and provides two interactive test programs for isolating and verifying the status LED display and individual LED mapping. All side animations, battery/RF/sleep LED shows, and RGB matrix effects are dead-stripped from the binary.

```bash
qmk flash -kb nuphy/halo75_v2/iso -km via -e CONSOLE_ENABLE=yes -e RGB_DEBUG=yes
```

`CONSOLE_ENABLE=yes` enables the USB console; `RGB_DEBUG=yes` silences HID/matrix event logging so the LED update stream is readable.

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

#### Battery Display Behaviour

The BATTERY program uses the same colour mapping as the real battery display, ported from NuPhy's original firmware (Halo65 V2, commit `f1856912d6`):

- **Steady state (not charging, bat ≥ 10%):** All 5 status LEDs light the same colour based on battery level. This matches the official manual — the colour indicates the level, not the number of lit segments.
- **Charging:** Breathing animation in amber (`#804000`) at quarter brightness, or shifting segment animation if `CHARGING_SHIFT` is defined.
- **Low battery (< 10%):** Red blink — all 5 LEDs flash red 6 times at 500ms intervals.

| Battery Range | Colour | Hex | Segments (charging anim only) |
|---------------|-------|-----|-------------------------------|
| ≤ 20% | Red | `FF0000` | 1 |
| ≤ 50% | Orange-Red | `FF2000` | 2 |
| ≤ 80% | Dark Orange | `804000` | 4 |
| > 80% | Green | `008000` | 5 |

#### Implementation Files

| File | Role |
|------|------|
| `rules.mk` | `RGB_DEBUG` build flag adds `-DRGB_DEBUG`; console remains an explicit QMK build override |
| `halo75_v2.c` | Console init, keycode interception in `process_record_kb`, debug render in `rgb_matrix_indicators_advanced_user`, `rgb_debug_task` call in `housekeeping_task_kb` |
| `side.c` | `rgb_debug_task` (console logger), `rgb_debug_render` (program renderer), `rgb_debug_cycle_program` / `rgb_debug_step_value` (step controls) |

## Keymaps

- **via** - VIA configurator compatible keymap
- **default** - Default keymap

## Notes

- This is a community-maintained fork of the NuPhy firmware
- Fixes include proper status LED protection and improved RGB matrix effect handling
- Wireless features require the 2.4GHz receiver
