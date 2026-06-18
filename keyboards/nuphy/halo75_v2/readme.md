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

## Keymaps

- **via** - VIA configurator compatible keymap
- **default** - Default keymap

## Notes

- This is a community-maintained fork of the NuPhy firmware
- Fixes include proper status LED protection and improved RGB matrix effect handling
- Wireless features require the 2.4GHz receiver
