# NuPhy Halo75 V2 — ANSI

Standard US layout variant of the NuPhy Halo75 V2.

## LED Matrix

The ANSI variant has the same LED structure as the ISO variant:
- **Main keyboard LEDs**: 0-82
- **Status LEDs**: 83-87 (system status, battery indicator)
- **Rim LEDs**: 88-126 (side lighting strip)

For detailed LED matrix mapping, see the main [Halo75 V2 readme](../readme.md).

## Building

```bash
qmk compile -kb nuphy/halo75_v2/ansi -km via
```

## Flashing

```bash
qmk flash -kb nuphy/halo75_v2/ansi -km via
```

## Keymaps

- **via** - VIA configurator compatible keymap
- **default** - Default keymap

## Notes

- This variant uses the standard ANSI layout
- LED mapping is similar to ISO but with different key positions
- All RGB matrix effects and status LED protection features apply
