# How an Uhlenbrock IB-Switch works on LocoNet

A recording of what a real IB-Switch and an Intellibox I send on LocoNet, message by message, and what Marklin-6040-LocoNet does with it. Recorded on 30 October 2026.

## Summary

- **At power-up the IB-Switch asks the command station where each of its turnouts is.** It uses one question per turnout (`OPC_SW_STATE`), one at a time, in the order of its key pairs. It waits for each answer (`OPC_LONG_ACK`) before asking the next.
- **The answers are its start signal.** Until the first answer comes, its lights stay dark and it repeats the first question every 3 to 3.5 seconds. No other message from the command station is needed: a computer that only answered the questions woke it up, and its lights showed exactly what was answered.
- **A key sends a switch command (`OPC_SW_REQ`) "on" when pressed and "off" when released.** The time between them is as long as the key is held.
- **An accepted switch command gets no answer.** The Intellibox only answers when it refuses one (`LONG_ACK` with code 0), for example while the track power is off.
- **After a refusal the IB-Switch repeats the same "on" about every 3 seconds.** It keeps the "off" back until the "on" is accepted. Keys pressed in the meantime are saved and sent, all at once, as soon as the command station accepts again.
- **Its lights follow switch commands from other devices** for its turnouts.
- **It asks only at its own power-up.** When the command station comes later, or the track power goes on, it does not ask again; its lights can stay out of date until it is unplugged and plugged in again.
- **The Intellibox I asks its own keyboard's turnouts the same way** after every track power on (turnouts 1 to 8), and answers itself. A turnout it has never switched, it reports as thrown.

## The setup

| Device | Details |
| --- | --- |
| Command station | Uhlenbrock Intellibox I (65000) |
| Panel | Uhlenbrock IB-Switch, 8 key pairs assigned (see below) |
| LocoNet to network | GCA101 LocoBuffer-UDP (MGV101) at 192.168.0.200. It passes every LocoNet message to the multicast group 224.0.0.1, port 1235, and every message sent to that group onto LocoNet ("LNUDP", as Rocrail uses it) |
| Recorder | A MacBook running [Hive-Rail](https://github.com/CuriousTimo/Hive-Rail) with `--loconet-udp`, logging every message with its time and sender |

Only the IB-Switch, the Intellibox and the GCA101 were on the LocoNet; devices were plugged in and out between the tests. The IB-Switch's key pairs were assigned to these turnouts (found from its questions, in this order):

| Key pair | 1 | 2 | 3 | 4 | 11 | 12 | 18 | 19 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Turnout | 111 | 112 | 137 | 131 | 121 | 122 | 126 | 127 |

The other key pairs were not assigned; they were never asked and their lights stayed dark.

## LocoNet messages in short

Every LocoNet message starts with an **opcode**, the only byte with bit 7 set. Bits 6 and 5 of the opcode give the length:

| Opcode | Length |
| --- | --- |
| `0x80` to `0x9F` | 2 bytes |
| `0xA0` to `0xBF` | 4 bytes |
| `0xC0` to `0xDF` | 6 bytes |
| `0xE0` to `0xFF` | the second byte gives the length |

The last byte is a **checksum**: all bytes of a message XOR-ed together, checksum included, give `0xFF`. For example `B0 6E 10 31`: `B0 ⊕ 6E ⊕ 10 = CE`, and `CE ⊕ 31 = FF`. Every message below was checked this way.

### `OPC_GPOFF` (0x82) and `OPC_GPON` (0x83): track power

| Bytes | Meaning |
| --- | --- |
| `82 7D` | Track power off (Intellibox STOP, red key) |
| `83 7C` | Track power on (Intellibox GO, green key) |

### `OPC_SW_REQ` (0xB0): switch a turnout

`B0 sw1 sw2 chk`

| Byte | Bits | Meaning |
| --- | --- | --- |
| `sw1` | `0 A6 A5 A4 A3 A2 A1 A0` | the lower 7 bits of the address |
| `sw2` | `0 0 DIR ON A10 A9 A8 A7` | `DIR` (0x20): 1 = closed (green), 0 = thrown (red). `ON` (0x10): 1 = output on, 0 = output off. Then the upper 4 bits of the address |

The address on the wire is the turnout number minus 1: turnout = `((sw2 & 0x0F) << 7 | sw1) + 1`.

| Bytes | Turnout | Direction | Output |
| --- | --- | --- | --- |
| `B0 6E 30 11` | 111 (`0x6E` = 110) | closed (green) | on |
| `B0 6E 20 01` | 111 | closed (green) | off |
| `B0 6E 10 31` | 111 | thrown (red) | on |
| `B0 6E 00 21` | 111 | thrown (red) | off |
| `B0 6F 30 10` | 112 | closed (green) | on |
| `B0 6F 20 00` | 112 | closed (green) | off |
| `B0 6F 10 30` | 112 | thrown (red) | on |
| `B0 6F 00 20` | 112 | thrown (red) | off |

### `OPC_SW_STATE` (0xBC): ask a turnout's position

`BC sw1 sw2 chk`, with the address as in `OPC_SW_REQ` and `DIR` and `ON` both 0. The command station answers with a `LONG_ACK`.

| Bytes | Turnout | Asked by |
| --- | --- | --- |
| `BC 6E 00 2D` | 111 (`0x6E` = 110) | IB-Switch, key pair 1 |
| `BC 6F 00 2C` | 112 | IB-Switch, key pair 2 |
| `BC 08 01 4A` | 137 (`1 << 7 \| 0x08` = 136) | IB-Switch, key pair 3 |
| `BC 02 01 40` | 131 (`1 << 7 \| 0x02` = 130) | IB-Switch, key pair 4 |
| `BC 78 00 3B` | 121 (`0x78` = 120) | IB-Switch, key pair 11 |
| `BC 79 00 3A` | 122 | IB-Switch, key pair 12 |
| `BC 7D 00 3E` | 126 | IB-Switch, key pair 18 |
| `BC 7E 00 3D` | 127 | IB-Switch, key pair 19 |
| `BC 00 00 43` … `BC 07 00 44` | 1 … 8 | the Intellibox's own keyboard, after track power on |

### `OPC_LONG_ACK` (0xB4): the answer to a question or a refusal

`B4 lopc ack1 chk`: `lopc` is the opcode being answered with bit 7 cleared; `ack1` is the answer.

| Bytes | Answers | Meaning |
| --- | --- | --- |
| `B4 3C 30 47` | `OPC_SW_STATE` (`0xBC & 0x7F` = `0x3C`) | **closed** (green) |
| `B4 3C 50 27` | `OPC_SW_STATE` | **thrown** (red) |
| `B4 30 00 7B` | `OPC_SW_REQ` (`0xB0 & 0x7F` = `0x30`) | **refused** (code 0): the command is not carried out |

The `LONG_ACK` does **not** name the turnout: it belongs to the question just before it. That is why the IB-Switch asks one turnout at a time.

For `OPC_SW_STATE`, bit 5 of `ack1` (`0x20`) is set for closed and clear for thrown, as JMRI reads it. This matches what we saw: after the last command "turnout 111 closed", the Intellibox answered `0x30` for 111, and the IB-Switch lit green for `0x30` and red for `0x50`. A turnout the Intellibox has never switched is answered `0x50`, thrown.

### Other messages seen

| Bytes | Meaning |
| --- | --- |
| `A0 01 00 5E` | `OPC_LOCO_SPD`: slot 1, speed 0. The Intellibox sends it about every 50 seconds |
| `BB 01 00 45` | `OPC_RQ_SL_DATA`: "send me slot 1" (sent by Hive-Rail) |
| `E7 0E 01 32 03 00 10 06 00 00 00 00 00 30` | `OPC_SL_RD_DATA`, 14 bytes: slot `01`, status `32` (in use, 14 speed steps), loco address `03`, speed `00`, direction and F0 `10` (forward, light on), track status `06` (power off; `07` with power on) |

### Not LocoNet: the GCA101's announcement

When something changed on its LocoNet (a device plugged in, track power on), the GCA101 sent one more datagram that is **not** a LocoNet message (its first byte has bit 7 clear):

```text
71 FF  C0 A8 00 C8  FF FF FF 00  00  4D 56 31 30 31  21 06
       192.168.0.200 255.255.255.0    "MV101"         (version?)
```

A program reading the UDP group must skip it; it is not a broken LocoNet message.

## What happened, test by test

Times are from the recording; all messages came through the GCA101 unless "Mac" is named.

### 1. IB-Switch alone, no command station

```text
13:37:23  BC 6E 00 2D   SW_STATE turnout 111?     (no answer)
13:37:26  BC 6E 00 2D   SW_STATE turnout 111?     (no answer)
13:37:29  BC 6E 00 2D   ...
          ... every 3.3 s, 20 times, until it was unplugged
```

Its lights stayed dark. It never went on to the next turnout.

### 2. Intellibox switched on, IB-Switch unplugged

```text
13:39:54  83 7C         GPON (GO)
13:39:54  BC 00 00 43   SW_STATE turnout 1?   (the Intellibox's own keyboard)
13:39:54  B4 3C 50 27   LONG_ACK thrown
13:39:54  BC 01 00 42   SW_STATE turnout 2?
13:39:54  B4 3C 50 27   LONG_ACK thrown
13:39:54  BC 02 00 41   SW_STATE turnout 3?
13:39:54  B4 3C 30 47   LONG_ACK closed
          ... turnouts 4 to 8: closed
13:39:54  83 7C         GPON again
13:39:57  82 7D         GPOFF (STOP)
```

The Intellibox I has a keyboard of 8 key pairs (turnouts 1 to 8). After every GO it asks their positions over LocoNet, like any other panel, and its command station part answers. When the Intellibox itself starts, it first sends `82 7D` (GPOFF) and 3 seconds later `83 7C` (GPON).

### 3. IB-Switch plugged in while the Intellibox runs: the power-up

```text
13:56:40  BC 6E 00 2D   SW_STATE turnout 111?   IB-Switch, key pair 1
13:56:40  B4 3C 30 47   LONG_ACK closed         Intellibox
13:56:40  BC 6F 00 2C   SW_STATE turnout 112?   key pair 2
13:56:40  B4 3C 50 27   LONG_ACK thrown
13:56:40  BC 08 01 4A   SW_STATE turnout 137?   key pair 3
13:56:40  B4 3C 50 27   LONG_ACK thrown
13:56:40  BC 02 01 40   SW_STATE turnout 131?   key pair 4
13:56:40  B4 3C 50 27   LONG_ACK thrown
13:56:40  BC 78 00 3B   SW_STATE turnout 121?   key pair 11
13:56:40  B4 3C 50 27   LONG_ACK thrown
13:56:40  BC 79 00 3A   SW_STATE turnout 122?   key pair 12
13:56:40  B4 3C 50 27   LONG_ACK thrown
13:56:40  BC 7D 00 3E   SW_STATE turnout 126?   key pair 18
13:56:40  B4 3C 50 27   LONG_ACK thrown
13:56:40  BC 7E 00 3D   SW_STATE turnout 127?   key pair 19
13:56:40  B4 3C 50 27   LONG_ACK thrown
          (nothing more)
```

All 8 questions and answers took well under a second. After the last answer the IB-Switch sent nothing more: no other message is part of its start. Turnout 111 was closed because that was the last command the Intellibox got for it (in test 6); the others had never been switched and are reported thrown.

In one earlier run (13:41:12) only the first 4 questions appeared. A GCA101 announcement was sent at the same moment; messages were probably lost there. It did not happen again.

### 4. IB-Switch with a computer as the command station

The Intellibox unplugged. A script on the Mac answered every `OPC_SW_STATE` through the GCA101: turnout 112 thrown, every other turnout closed.

```text
13:47:58  BC 6E 00 2D   turnout 111?    IB-Switch
13:47:58  B4 3C 30 47   closed          Mac
13:48:01  BC 6E 00 2D   turnout 111?    (asked again: the first answer was apparently not taken)
13:48:01  B4 3C 30 47   closed          Mac
13:48:01  BC 6F 00 2C   turnout 112?
13:48:01  B4 3C 50 27   thrown          Mac
13:48:02  BC 08 01 4A   turnout 137?  ->  B4 3C 30 47  closed
13:48:02  BC 02 01 40   turnout 131?  ->  closed
13:48:02  BC 78 00 3B   turnout 121?  ->  closed
13:48:03  BC 79 00 3A   turnout 122?  ->  closed
13:48:03  BC 7D 00 3E   turnout 126?  ->  closed
13:48:03  BC 7E 00 3D   turnout 127?  ->  closed
```

The IB-Switch lit key pair 2 red and key pairs 1, 3, 4, 11, 12, 18 and 19 green: exactly as answered. **The answers alone start it.**

### 5. Keys, with the command station accepting

(Test 4's setup: the script answered questions but not switch commands, so every command was accepted.)

```text
13:49:46  B0 6E 30 11   turnout 111 closed, on    key pair 1 green pressed
13:49:48  B0 6E 20 01   turnout 111 closed, off   released 2 s later
13:49:50  B0 6E 10 31   turnout 111 thrown, on    key pair 1 red, a tap
13:49:50  B0 6E 00 21   turnout 111 thrown, off
13:49:53  B0 6F 30 10   turnout 112 closed, on    key pair 2 green pressed
13:49:56  B0 6F 20 00   turnout 112 closed, off   released 3 s later
```

The "off" follows the key release, however long the key is held. Its lights changed at once (key pair 1 red, key pair 2 green). The IB-Switch's Special Option 1 can set a fixed switching time instead; how it was set on this IB-Switch was not checked.

### 6. Keys while the track power is off: refused, repeated, saved

The Intellibox on, track power off (last STOP at 13:39:57).

```text
13:42:55  B0 6E 10 31   turnout 111 thrown, on    key pair 1 red pressed
13:42:55  B4 30 00 7B   LONG_ACK: refused         Intellibox
13:42:57  B0 6E 10 31   the same "on" again
13:42:57  B4 30 00 7B   refused
13:43:00  B0 6E 10 31   ->  refused
13:43:04  B0 6E 10 31   ->  refused
          ... every 2 to 4 s (about 3 s), 14 times in 40 s
13:43:28  82 7D         GPOFF (STOP pressed again)
13:43:31  B0 6E 10 31   (no answer seen this time)
13:43:34  B0 6E 10 31   ->  refused
13:43:35  83 7C         GPON (GO)
13:43:35  BC 00 00 43 … the Intellibox's keyboard asks turnouts 1 to 8
13:43:36  B0 6E 10 31   turnout 111 thrown, on     accepted: no answer
13:43:36  B0 6E 00 21   turnout 111 thrown, off
13:43:37  B0 6E 30 11   turnout 111 closed, on     saved key press
13:43:37  B0 6E 20 01   turnout 111 closed, off
13:43:37  B0 6F 10 30   turnout 112 thrown, on     saved key press
13:43:37  B0 6F 00 20   turnout 112 thrown, off
```

- Without track power the Intellibox refuses every switch command at once with `B4 30 00 7B`.
- The IB-Switch repeats the refused "on" about every 3 seconds (its Special Option 6), without a limit while we watched, and does not send its "off".
- Keys pressed meanwhile are saved. When the command station accepts again, everything goes out quickly: the waiting "on" with its "off", then each saved key as "on" and "off".
- An accepted command gets no answer at all.

### 7. Another device switches a turnout

The IB-Switch's light for key pair 1 showed red. The Mac sent through the GCA101:

```text
13:51:01  B0 6E 30 11   turnout 111 closed, on    Mac
13:51:01  B0 6E 20 01   turnout 111 closed, off   Mac, half a second later
```

The IB-Switch's light for key pair 1 went green. **Its lights follow every device's switch commands for its turnouts.**

### 8. The command station comes after the IB-Switch

The IB-Switch running (lit by test 4), the Intellibox plugged in:

```text
13:55:31  82 7D         GPOFF   the Intellibox starts
13:55:34  83 7C         GPON
13:55:34  BC 00 … BC 07 the Intellibox's own keyboard, answered by itself
13:55:34  83 7C         GPON
```

**The IB-Switch asked nothing.** Neither the new command station nor the track power on made it ask its turnouts again; its lights kept what the script had answered, not what the Intellibox knew. Only unplugging and plugging it in again (test 3) brought them up to date.

## The IB-Switch's behaviour as rules

1. At power-up: for each assigned key pair, in key order, send `OPC_SW_STATE` and wait for `LONG_ACK 3C`. Set the lights from `ack1` (0x20 set: green; clear: red). Without an answer, repeat the same question every ~3.3 s; the lights stay dark.
2. Key pressed: `OPC_SW_REQ` with `ON`. Key released: the same with `ON` cleared (or after the time of Special Option 1). Change the lights when the command goes out.
3. `LONG_ACK 30 00` right after its command: refused. Repeat the "on" every ~3 s; hold back the "off"; save further key presses; send everything once a command is accepted.
4. Any `OPC_SW_REQ` "on" for an assigned turnout, from any device: change the lights.
5. Nothing else. In particular, it does not ask again on `OPC_GPON` or when a command station appears.

## What Marklin-6040-LocoNet does

| Rule | IB-Switch | Marklin-6040-LocoNet 0.3.0 |
| --- | --- | --- |
| Lights at power-up | Asks each turnout (`OPC_SW_STATE`), one at a time | **The same.** Every assigned turnout of every Keyboard it serves, one at a time; a turnout on several key pairs is asked once |
| No command station | Repeats the first question every ~3.3 s, lights dark | **The same:** repeats every 3 s; `status` says "no command station answers yet" |
| Track power on (`OPC_GPON`) | Asks nothing | **Asks again**, 1 s after the GPON (after the Intellibox's own keyboard questions), so the lights can never stay out of date |
| Assignment changed, Keyboard found later | — | Asks again |
| Answers to other devices' questions | — | **Used too:** an answer after an IB-Switch's or the Intellibox's question also sets the lights of that turnout |
| Key pressed | `OPC_SW_REQ` on | The same |
| Coil off | On release (as tested) or after Special Option 1 | After 200 ms (`option coil`), or on release with `option coil 0` |
| Refused | Repeats every ~3 s, saves further keys | Repeats every 3 s, up to 10 times, then the lights go back. With `option hold on` (the factory setting) a key is not sent at all while LocoNet reports power off, and its lights go back at once; `option hold off` sends it and lets the retry work as on the IB-Switch |
| Other devices' commands | Lights follow | The same, plus `OPC_SW_REP` outputs and occupancy (`OPC_INPUT_REP`) |

The deliberate difference is **`option hold`**: the IB-Switch saves keys pressed without track power and switches them all later, when GO is pressed, which can surprise someone who has forgotten them. The firmware does not send them, and shows the turnout's real position again instead.

## Open questions

- How the tested IB-Switch's Special Options 1 (coil time) and 6 (retry) were set.
- What other command stations (Digitrax, DCC-EX with LocoNet, Rocrail's virtual command station) answer to `OPC_SW_STATE`, especially for a turnout they have never switched.
- Whether the GCA101 loses messages when it sends its announcement (test 3, first run).
