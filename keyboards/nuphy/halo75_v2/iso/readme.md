# NuPhy Halo75 V2 — ISO Nordic

## LED Matrix Reference

Each key / LED zone is mapped to its address in the LED matrix (index 0–127).

### Main Key Area

```
┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
│ 0  │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │ 7  │ 8  │ 9  │ 10 │ 11 │ 12 │ 13 │ 15 │ 14 │
│ESC │ F1 │ F2 │ F3 │ F4 │ F5 │ F6 │ F7 │ F8 │ F9 │F10 │F11 │F12 │PRT │INS │DEL │
└────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬─────────┬────┐
│ 16 │ 17 │ 18 │ 19 │ 20 │ 21 │ 22 │ 23 │ 24 │ 25 │ 26 │ 27 │ 28 │   29    │ 30 │
│ §  │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │ 7  │ 8  │ 9  │ 0  │ +  │ ´  │BACKSPACE│HOME│
└────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴─────────┴────┘
┌──────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬──────┬────┐
│  31  │ 32 │ 33 │ 34 │ 35 │ 36 │ 37 │ 38 │ 39 │ 40 │ 41 │ 42 │ 43 │  58  │ 45 │
│ TAB  │ Q  │ W  │ E  │ R  │ T  │ Y  │ U  │ I  │ O  │ P  │ Å  │ ¨  │ENTER │END │
└──────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴──────┴────┘
┌───────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬──────┬────┐
│  46   │ 47 │ 48 │ 49 │ 50 │ 51 │ 52 │ 53 │ 54 │ 55 │ 56 │ 57 │ 44 │  58  │ 59 │
│CAPSLK │ A  │ S  │ D  │ F  │ G  │ H  │ J  │ K  │ L  │ Ö  │ Ä  │ '  │ENTER │PGUP│
└───────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴──────┴────┘
┌────────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬──────────┬────┬────┐
│  60    │127 │ 61 │ 62 │ 63 │ 64 │ 65 │ 66 │ 67 │ 68 │ 69 │ 70 │    71    │ 72 │ 73 │
│ LSHIFT │ <> │ Z  │ X  │ C  │ V  │ B  │ N  │ M  │ ,  │ .  │ -  │  RSHIFT  │ ↑  │PGDN│
└────────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴──────────┴────┴────┘
┌──────┬──────┬──────┬───────────────────────┬──────┬──────┬────┬────┬────┐
│  74  │  75  │  76  │          77           │  78  │  79  │ 80 │ 81 │ 82 │
│LCTRL │ LOPT │ LCMD │        SPACE          │ RCMD │  FN  │ ←  │ ↓  │ →  │
└──────┴──────┴──────┴───────────────────────┴──────┴──────┴────┴────┴────┘
```

**Notes:**

- The diagram shows the **physical key positions**; each cell is labelled with its **LED hardware index** (0–127). An index can be anywhere on the board since this is an LED matrix.
- **INS (15)** and **DEL (14)** are index-swapped relative to their physical order (INS sits left of DEL).
- **ENTER (58)** is the ISO L-shaped key; it spans the QWERTY and home rows, so `58` appears in both rows.
- **'** is index `44` and physically sits on the home row between **Ä (57)** and **ENTER (58)**.
- **`<>`** is index `127` and physically sits between **LSHIFT (60)** and **Z (61)**.
- **PRT (13)** is the screenshot key.

### Status LED Strip (5 segments)

| Index | Segment |
|------:|---------|
| 83 | Status seg 1 |
| 84 | Status seg 2 |
| 85 | Status seg 3 |
| 86 | Status seg 4 |
| 87 | Status seg 5 |

### Rim LED Strip (36 segments)

| Index | Segment |
|------:|---------|
| 88  | Rim seg 1  |
| 89  | Rim seg 2  |
| 90  | Rim seg 3  |
| 91  | *(unused)* |
| 92  | *(unused)* |
| 93  | Rim seg 4  |
| 94  | Rim seg 5  |
| 95  | Rim seg 6  |
| 96  | Rim seg 7  |
| 97  | Rim seg 8  |
| 98  | Rim seg 9  |
| 99  | Rim seg 10 |
| 100 | Rim seg 11 |
| 101 | Rim seg 12 |
| 102 | Rim seg 13 |
| 103 | Rim seg 13 |
| 104 | Rim seg 14 |
| 105 | Rim seg 15 |
| 106 | Rim seg 16 |
| 107 | Rim seg 17 |
| 108 | Rim seg 18 |
| 109 | Rim seg 19 |
| 110 | Rim seg 20 |
| 111 | Rim seg 21 |
| 112 | Rim seg 22 |
| 113 | Rim seg 23 |
| 114 | Rim seg 24 |
| 115 | Rim seg 25 |
| 116 | Rim seg 26 |
| 117 | Rim seg 27 |
| 118 | Rim seg 28 (Halo logo) |
| 119 | Rim seg 29 |
| 120 | Rim seg 30 |
| 121 | Rim seg 31 |
| 122 | Rim seg 32 |
| 123 | Rim seg 33 |
| 124 | Rim seg 34 |
| 125 | Rim seg 35 |
| 126 | Rim seg 36 |

### Extra Key

| Index | Key |
|------:|-----|
| 127 | `<>` (ISO key between LSHIFT and Z) |

## Full Index Summary

| Index | Key / LED |
|------:|-----------|
| 0   | ESC |
| 1   | F1 |
| 2   | F2 |
| 3   | F3 |
| 4   | F4 |
| 5   | F5 |
| 6   | F6 |
| 7   | F7 |
| 8   | F8 |
| 9   | F9 |
| 10  | F10 |
| 11  | F11 |
| 12  | F12 |
| 13  | Screenshot |
| 14  | DEL |
| 15  | INS |
| 16  | § |
| 17  | 1 |
| 18  | 2 |
| 19  | 3 |
| 20  | 4 |
| 21  | 5 |
| 22  | 6 |
| 23  | 7 |
| 24  | 8 |
| 25  | 9 |
| 26  | 0 |
| 27  | + |
| 28  | ´ |
| 29  | BACKSPACE |
| 30  | HOME |
| 31  | TAB |
| 32  | Q |
| 33  | W |
| 34  | E |
| 35  | R |
| 36  | T |
| 37  | Y |
| 38  | U |
| 39  | I |
| 40  | O |
| 41  | P |
| 42  | Å |
| 43  | ¨ |
| 44  | ' |
| 45  | END |
| 46  | CAPSLOCK |
| 47  | A |
| 48  | S |
| 49  | D |
| 50  | F |
| 51  | G |
| 52  | H |
| 53  | J |
| 54  | K |
| 55  | L |
| 56  | Ö |
| 57  | Ä |
| 58  | ENTER |
| 59  | PGUP |
| 60  | LSHIFT |
| 61  | Z |
| 62  | X |
| 63  | C |
| 64  | V |
| 65  | B |
| 66  | N |
| 67  | M |
| 68  | , |
| 69  | . |
| 70  | - |
| 71  | RSHIFT |
| 72  | ARWUP |
| 73  | PGDWN |
| 74  | LCTRL |
| 75  | LOPT |
| 76  | LCMD |
| 77  | SPACE |
| 78  | RCMD |
| 79  | FN |
| 80  | ARWL |
| 81  | ARWD |
| 82  | ARWR |
| 83  | Status seg 1 |
| 84  | Status seg 2 |
| 85  | Status seg 3 |
| 86  | Status seg 4 |
| 87  | Status seg 5 |
| 88  | Rim seg 1 |
| 89  | Rim seg 2 |
| 90  | Rim seg 3 |
| 91  | *(unused)* |
| 92  | *(unused)* |
| 93  | Rim seg 4 |
| 94  | Rim seg 5 |
| 95  | Rim seg 6 |
| 96  | Rim seg 7 |
| 97  | Rim seg 8 |
| 98  | Rim seg 9 |
| 99  | Rim seg 10 |
| 100 | Rim seg 11 |
| 101 | Rim seg 12 |
| 102 | Rim seg 13 |
| 103 | Rim seg 13 |
| 104 | Rim seg 14 |
| 105 | Rim seg 15 |
| 106 | Rim seg 16 |
| 107 | Rim seg 17 |
| 108 | Rim seg 18 |
| 109 | Rim seg 19 |
| 110 | Rim seg 20 |
| 111 | Rim seg 21 |
| 112 | Rim seg 22 |
| 113 | Rim seg 23 |
| 114 | Rim seg 24 |
| 115 | Rim seg 25 |
| 116 | Rim seg 26 |
| 117 | Rim seg 27 |
| 118 | Rim seg 28 (Halo logo) |
| 119 | Rim seg 29 |
| 120 | Rim seg 30 |
| 121 | Rim seg 31 |
| 122 | Rim seg 32 |
| 123 | Rim seg 33 |
| 124 | Rim seg 34 |
| 125 | Rim seg 35 |
| 126 | Rim seg 36 |
| 127 | `<>` |
