# Arduino Nano LocoNet & DCC Interface v3

The base module for LocoNet projects with an Arduino Nano: LocoNet in and out, a DCC input, I2C for servo drivers and expansion boards, and two expansion headers. It is the successor of the [v2](../Arduino%20LocoNet-DCC%20Interface%20v2) and the base of [Marklin-6040-LocoNet](../Marklin-6040-LocoNet), together with the [Märklin I2C Expansion v1](../Marklin%20I2C%20Expansion%20v1).

Designed in EAGLE 9.6.2: the schematic and the board are in [`Eagle/`](Eagle). Board 108.9 × 47 mm, two layers, through-hole parts only.

## On the board

| Part | What it does | Nano pin |
| --- | --- | --- |
| 2 × LocoNet socket (RJ12) | LocoNet in and out, wired straight through | |
| LocoNet receive | LM393 comparator: switches at about 4.5 V rising and 3.5 V falling on the LocoNet line; red LED shows LocoNet traffic | **D8** (the input-capture pin the LocoNet library needs) |
| LocoNet send | BC337 transistor that pulls the LocoNet line low | **D7** |
| DCC input | 6N137 optocoupler, DCC from the track or a booster on the two-pole terminal | **D2** (interrupt, as NmraDcc needs) |
| Power | 7805 regulator, fed from LocoNet RailSync or a separate power input (both through diodes); green LED | |
| I2C | 4.7 kΩ pull-ups (jumper), a 6-pin header for a PCA9685 servo board | A4 (SDA), A5 (SCL) |
| Blue 4-pole terminal | 5 V, GND, and either I2C or D5/D6, chosen with jumpers | |
| EXP1 (10-pin) | D2 (DCC), D3, D4, D5, D6, D9, D10, D11, 5 V, GND | |
| EXP2 (10-pin) | D12, D13, A0, A1, A2, A3, SDA, SCL, 5 V, GND; the Märklin I2C Expansion connects here | |

## Jumpers

| Jumper | Closed |
| --- | --- |
| **5V from 7805** | The 7805 feeds the Nano's 5 V, bypassing the Nano's own small regulator |
| **Vin from LocoNet** | The Nano's VIN gets LocoNet RailSync or the power input |
| **i2c pullup** | 4.7 kΩ pull-ups on SDA and SCL; needed for the Märklin Keyboard bus |
| **SCL/SDA – D5/D6** | Whether the blue terminal carries I2C or D5 and D6 |

## To check before building more

Points from a review of the v3 design; none of them stops the board from working on the bench, but they are worth looking at for a v4:

- **DCC optocoupler resistor:** 10 kΩ gives about 1.5 mA at 16 V DCC, while a 6N137 is only guaranteed to switch at about 5 mA. 1.5 to 2.2 kΩ (0.25 W) is safer.
- **Unused half of the LM393:** both inputs on GND can let its output oscillate; one input to the reference voltage is the usual fix.
- **Comparator output load:** the 1 kΩ pull-up and the 470 Ω LED together draw about 11 mA from the LM393's output, at the upper end of what it does comfortably.
- **No protection** (TVS diode or polyfuse) on LocoNet.
- **Power budget:** the 7805 runs warm when it also has to feed expansion boards; heavy loads are better fed separately.
