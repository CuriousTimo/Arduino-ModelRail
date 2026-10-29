// Key map: what every key pair does, written down here and loaded together with the
// program. The other way is the Serial Monitor (see the README, "Setting up"); both can
// be used together.
//
// How it works:
// - Set kUseKeyMap to true and fill in your Keyboards below, then load the program onto
//   the Nano as usual.
// - At startup the Nano copies this key map into its memory, but only when it has
//   changed since the last time. Changes made on the Serial Monitor afterwards stay
//   until you load a changed key map again; `keymap load` copies it again on purpose.
// - Keyboards not listed here keep their setup.
//
// One line per Keyboard: first its number (0 to 15, from the switches on its back), then
// its 16 key pairs from left to right:
//   a number      the turnout it switches (1 to 2048), the number you would type on the
//                 Intellibox
//   0             the key pair does nothing
//   SENSOR(33)    the key pair shows track section 33 (1 to 4096): red occupied, green free

#ifndef KEY_MAP_H
#define KEY_MAP_H

#include <Arduino.h>

// true: load the key map below. false: set up on the Serial Monitor only.
const bool kUseKeyMap = false;

#define SENSOR(number) (0x8000u | (number))

struct KeyMapLine {
  uint8_t keyboard;
  uint16_t keys[16];
};

const KeyMapLine kKeyMap[] PROGMEM = {
    // Keyboard 0 switches turnouts 1 to 16.
    {0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},

    // Keyboard 1: a station. Key pairs 1 to 8 switch its turnouts, 9 and 10 do nothing,
    // 11 to 16 show its six tracks.
    {1, {101, 102, 103, 104, 105, 106, 107, 108, 0, 0,
         SENSOR(33), SENSOR(34), SENSOR(35), SENSOR(36), SENSOR(37), SENSOR(38)}},
};

#endif
