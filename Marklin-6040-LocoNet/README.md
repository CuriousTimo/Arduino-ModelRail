# Marklin-6040-LocoNet

Use a **Märklin Keyboard 6040** as a LocoNet switch panel, the way an **Uhlenbrock IB-Switch** works: its keys switch turnouts over LocoNet, and its lights show the turnouts' positions, whoever switched them (an Intellibox, Rocrail, a throttle or another panel). A key pair can also show a **LocoNet occupancy detector** instead: red occupied, green free.

An Arduino Nano takes the place of the Märklin central (6021) on the Keyboards' I2C bus. It answers the Keyboards as the central would and turns each key into a LocoNet switch command. Up to 16 Keyboards can share the bus, each with its own number.

The Keyboards stay unmodified. [Loconet_6040_PCA9685](../Loconet_6040_PCA9685) in this repository takes the other way: it replaces a single Keyboard's electronics with an Arduino.

> **Status:** version 0.1.0, built and unit-tested on a computer; not yet tested with a real Keyboard 6040 or LocoNet.

## Hardware

- **Arduino Nano LocoNet/DCC Interface v3**: the LocoNet base module with an Arduino Nano (LocoNet receive on D8, send on D7).
- **Märklin I2C Expansion Board v1**: connects to the interface board's **EXP2** with a 10-pin ribbon cable, and to the Keyboards through their DIN 41612 side connectors (left or right). Its step-up converter makes the Keyboards' 8 V.

Both boards are my own designs (Curious Timo, Losse Eindj.es), made in Eagle.

Before switching on:

1. Close the **"i2c pullup"** jumper on the interface board: the Keyboard bus needs pull-ups to 5 V.
2. Power the Keyboards from the expansion board's **DC-IN** with a 5 V supply and **JP1 open**. Each Keyboard draws about 120 mA at 8 V; that is too much for the interface board's 7805 or for LocoNet. With JP1 closed, DC-IN is connected to the Nano's 5 V, so it must never get more than 5 V.
3. Give every Keyboard its own number with the DIP switches on its back (0 to 15).

## Install

1. In the Arduino IDE, install the **LocoNet** library by Alex Shepherd et al. (mrrwa) from the Library Manager. `Wire` and `EEPROM` come with the IDE.
2. Open `Marklin-6040-LocoNet.ino`.
3. Choose **Arduino Nano** as the board (with a clone, processor **ATmega328P (Old Bootloader)**) and upload.
4. Open the Serial Monitor at **115200 baud** and set up the Keyboards there (below). The settings are kept in the Nano's EEPROM, so they survive a power cycle and a new upload.

With the Arduino command line:

```sh
arduino-cli lib install LocoNet
arduino-cli compile --fqbn arduino:avr:nano .
arduino-cli upload --fqbn arduino:avr:nano:cpu=atmega328old -p /dev/cu.usbserial-XXXX .
```

## Finding the Keyboards

At startup the Nano scans the bus: every Keyboard acknowledges its own I2C address, 0x10 plus its DIP switch number, where the central's answers reach it. The terminal lists the Keyboards it found, and `scan` repeats it. A Keyboard that did not answer the scan still counts as found as soon as it sends a key.

```text
Keyboards found: 0 1 3
  keyboard 0: switches 1 to 16
  keyboard 1: switches 17 to 32
  keyboard 3: switches 49 to 64
```

## Setting up on the terminal

Open the Serial Monitor (or any serial terminal) at **115200 baud**; any line ending works. Keyboards are numbered **0 to 15** as set by their DIP switches, not by their position on the bus; key pairs are **1 to 16**, left to right.

Without any setup, Keyboard *n* switches LocoNet turnouts *n* × 16 + 1 to *n* × 16 + 16: Keyboard 0 switches 1 to 16, Keyboard 1 switches 17 to 32 (the `auto` option). Give a Keyboard its own assignment with `set`:

```text
> set 2 all 101
Keyboard 2, on the bus, own assignment
  key:         1     2     3     4     5     6     7     8     9    10    11    12    13    14    15    16
  switch:    101   102   103   104   105   106   107   108   109   110   111   112   113   114   115   116
> set 2 5 0
> set 2 7 210
> sensor 2 9 33
```

| Command | What it does |
| --- | --- |
| `help` | The commands |
| `version` | The firmware version |
| `status` | Track power, Keyboards on the bus and with their own assignment, lost messages, LocoNet counters |
| `scan` | Look for Keyboards on the bus again |
| `show` | The assignment of every Keyboard that is on the bus or has its own |
| `show <kb>` | One Keyboard's 16 key pairs and their LocoNet addresses |
| `set <kb> <key> <address>` | A key pair switches a LocoNet address, 1 to 2048; `0` switches nothing (the lights still follow the key) |
| `set <kb> all <first>` | Key pairs 1 to 16 switch *first* to *first* + 15 |
| `sensor <kb> <key> <address>` | A key pair shows LocoNet sensor 1 to 4096 instead of switching: red occupied, green free |
| `sensor <kb> all <first>` | Key pairs 1 to 16 show sensors *first* to *first* + 15, for example a 16-section detector such as a BDL16 |
| `clear <kb>` | The Keyboard goes back to the automatic assignment |
| `option` | The options and their state |
| `option <name> on\|off` | `auto` (automatic assignment), `stopgo` (Märklin STOP/GO lines to LocoNet power), `hold` (no keys while track power is off), `log` (log keys and switch commands), `watchdog` (restart the Nano if it hangs) |
| `option coil <ms>` | How long a turnout coil stays on, 0 to 12750 ms in steps of 50 (default 200); `0` switches it off when the key is released |
| `reset yes` | Factory settings: every Keyboard automatic, options as in `Config.h` |

The first `set` or `sensor` on a Keyboard copies the assignment it had, so its other key pairs stay as they were. `show` marks sensors with an `s`, for example `s33`. Several key pairs may switch the same turnout; all of them show its position. Red is **thrown** and green is **closed**, as on an Intellibox.

`Config.h` holds the pins and the factory settings that `reset yes` returns to.

## How it behaves

| On the Keyboard | On LocoNet |
| --- | --- |
| Press a key | `OPC_SW_REQ` coil on, closed (green) or thrown (red); the lights change once the command is on the bus |
| 200 ms later | `OPC_SW_REQ` coil off for the same turnout, however long the key is held (`option coil`, as the IB-Switch's Special Option 1) |
| Lights change | Any `OPC_SW_REQ` coil-on for an assigned address, or an `OPC_SW_REP` with the outputs of a decoder that reports them |
| Sensor lights change | `OPC_INPUT_REP` from an occupancy detector, a track contact or the Intellibox's s88 modules: red occupied, green free |
| Press a sensor key | Nothing is sent; the lights keep showing the sensor |

- **Track power off:** while LocoNet reports power off (`OPC_GPOFF`), a key is not sent and its lights go back to the turnout's last known position (`option hold`).
- **Refused command:** when the command station answers a switch command with a refusal (`LONG_ACK`, because its buffer is full or it is in STOP), the firmware sends it again after 3 s, up to 10 times, as an IB-Switch does (its Special Option 6). If it still fails, the lights go back to the turnout's last known position. A new key press or another device switching the turnout cancels the retry.
- **No LocoNet:** if a command cannot be sent, the lights stay as they were. For a turnout whose position is not known yet, the Keyboard gets no answer and asks again about once a second, as it does with a Märklin central in STOP.
- **Releasing a key:** with `option coil 0` the coil goes off on the key release instead. The Keyboard's release names only the decoder (four key pairs), so the firmware then switches off the coil of the key last pressed on that decoder; the fixed coil time avoids that guess.
- **STOP and GO:** the Märklin bus STOP and GO lines can switch LocoNet power off and on (`option stopgo on`). This is **off** until the lines are measured: a floating line would switch the whole layout's power at random.
- **Status LED (D13):** on while track power is on or not yet known, slow blink while it is off, a short flicker on every key or switch command.
- **Watchdog:** if the firmware hangs for 2 s, for example on a stuck I2C bus, the Nano restarts and says so on the terminal (`option watchdog`). Some Nano clones with the old bootloader do not come back from a watchdog restart by themselves; a power cycle does, and `option watchdog off` avoids it.

## Log

With `option log on` (the default), the terminal logs every key and every LocoNet switch command for an assigned address:

```text
KEY: keyboard 0 key 3 green -> LocoNet switch 3 closed: Done
LN: switch 17 thrown -> keyboard 1 key 1
LN: sensor 33 occupied -> keyboard 2 key 9
LN: track power off
REFUSED: will send again in 3 s: switch 5 thrown
RETRY: switch 5 thrown
```

## Tests

The Keyboard message format (`Keyboard.h`) has host tests that run without an Arduino:

```sh
cd test
c++ -std=c++11 -Wall -Wextra -I.. keyboard_test.cpp -o keyboard_test && ./keyboard_test
```

## Compared with an IB-Switch

| | IB-Switch | Marklin-6040-LocoNet |
| --- | --- | --- |
| Key pairs | 20, × 2 levels | 16 per Keyboard, up to 16 Keyboards |
| Turnout addresses | 1 to 2048 | 1 to 2048 |
| Lights follow other devices | Yes | Yes |
| Coil time | Special Option 1, 200 ms | `option coil`, 200 ms |
| Retry after a refusal | Special Option 6, 3 s | 3 s, up to 10 times |
| Occupancy display | No | `sensor` |
| Sensors that press a key | Yes | Not yet |
| Routes | 40 × 20 steps, pauses, sub-routes, blocking | Not yet |
| Setup | On its own keys | USB terminal |

## Ideas for later

- **Sensors that press a key**, as on an IB-Switch: a sensor turning occupied or free switches a turnout or a route.
- **Routes** like the Märklin Memory 6043 and the IB-Switch: a key pair or a sensor switches a list of turnouts, one after another. Sized for a few Keyboards: a shared pool of about 200 steps (for example 16 routes of 12 turnouts) fits in the Nano's EEPROM next to the key assignment. The IB-Switch's 40 routes × 20 steps would need 1.6 KB and does not fit; a 24C256 EEPROM chip on the I2C bus would lift that limit. On a 6040, an active route shows red and a free one green: its key pairs cannot show the IB-Switch's yellow.
- **Lights at startup:** find out how a real IB-Switch gets its lights at power-up (sniff its LocoNet traffic) and do the same. Sensors are only known once they report; some detectors (Digitrax) report all of them on a request.
- **Programming from the Keyboard itself**, as on an IB-Switch: for example, a terminal command that waits for a key press and gives that key pair the next address.
- **LNCV** (Uhlenbrock's LocoNet configuration variables), so the Intellibox can program the assignment like an IB-Switch's.

## Background

The Keyboard 6040 is an I2C master. It sends `receiver sender data` to the central at address 0x7F and waits for the same data back; `Keyboard.h` describes the format in detail. It was documented by the [c6021light](https://github.com/deltaphi/c6021light) project (a Märklin CAN version of the same idea) and on [Dr. König's Märklin-Digital-Page](http://www.drkoenig.de/digital/i2c.htm). No code was taken from c6021light.

## License

[Apache License 2.0](LICENSE). The LocoNet library is LGPL-2.1 or later.
