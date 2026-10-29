// The Märklin Keyboard 6040 on the I2C bus: message format and turnout numbers.
//
// Plain C++ without Arduino, so the host tests in test/ can check it.
//
// A Keyboard is an I2C master that sends three bytes to the central:
//   receiver  0xFE  the central's address 0x7F, sent as the I2C address byte
//   sender    0x20 | keyboard << 1  (keyboard 0 to 15, from its DIP switches)
//   data      0 0 D1 D0 P O1 O0 G
//             D: decoder 0 to 3 of this keyboard, O: output 0 to 3 of that decoder,
//             P: power (1 key pressed, 0 key released), G: 1 green, 0 red
// and then waits for the central's answer, the same data sent back to it:
//   receiver  0x20 | keyboard << 1  (I2C address 0x10 + keyboard)
//   sender    0xFE
//   data      as above
// Until the answer comes, the keyboard repeats its request about once a second and
// ignores its other keys. An answer it did not ask for still sets its lights.
//
// On release the keyboard only names the decoder: the output and colour bits are 0,
// so the central has to remember which key of that decoder was pressed.
//
// Source of the format: the c6021light project (github.com/deltaphi/c6021light) and
// Dr. König's Märklin-Digital-Page.

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

namespace keyboard {

/// The central's 7-bit I2C address; this firmware takes it.
const uint8_t kCentralAddress = 0x7F;
/// The central's address as it appears in the sender byte.
const uint8_t kCentralSender = kCentralAddress << 1;
/// Keyboard 0's 7-bit I2C address; keyboard n is at 0x10 + n.
const uint8_t kFirstKeyboardAddress = 0x10;

const uint8_t kKeyboards = 16;
/// 16 keyboards with 16 key pairs each.
const uint16_t kTurnouts = 256;

const uint8_t kTypeMask = 0x60;
const uint8_t kTypeAccessory = 0x20;
const uint8_t kSenderMask = 0x1E;

const uint8_t kGreen = 0x01;
const uint8_t kOutputMask = 0x06;
const uint8_t kPower = 0x08;
const uint8_t kDecoderMask = 0x30;

/// Whether a sender byte belongs to a keyboard (an accessory message), not to a
/// loco control.
inline bool isKeyboard(uint8_t sender) { return (sender & kTypeMask) == kTypeAccessory; }

/// The keyboard number 0 to 15 from a sender byte.
inline uint8_t keyboardOf(uint8_t sender) { return (sender & kSenderMask) >> 1; }

/// A keyboard's 7-bit I2C address, for the answer.
inline uint8_t i2cAddressOf(uint8_t keyboard) { return kFirstKeyboardAddress | (keyboard & 0x0F); }

inline bool isPressed(uint8_t data) { return (data & kPower) != 0; }
inline bool isGreen(uint8_t data) { return (data & kGreen) != 0; }

/// The decoder of this keyboard, 0 to 3.
inline uint8_t decoderOf(uint8_t data) { return (data & kDecoderMask) >> 4; }

/// The key pair 0 to 255 over all keyboards: keyboard 0 has 0 to 15, keyboard 1 has 16
/// to 31. Only meaningful for a key press.
inline uint8_t turnoutOf(uint8_t keyboard, uint8_t data) {
  return ((keyboard & 0x0F) << 4) | (decoderOf(data) << 2) | ((data & kOutputMask) >> 1);
}

/// The decoder 0 to 63 over all keyboards, which a release names.
inline uint8_t decoderIndexOf(uint8_t keyboard, uint8_t data) {
  return ((keyboard & 0x0F) << 2) | decoderOf(data);
}

/// The keyboard that shows a turnout.
inline uint8_t keyboardOfTurnout(uint8_t turnout) { return turnout >> 4; }

/// The data byte that sets a turnout's lights. A release form (pressed false) keeps
/// only the decoder bits, as the keyboard sends it.
inline uint8_t dataFor(uint8_t turnout, bool green, bool pressed) {
  uint8_t data = ((turnout >> 2) & 0x03) << 4;
  if (!pressed) {
    return data;
  }
  data |= (turnout & 0x03) << 1;
  data |= kPower;
  if (green) {
    data |= kGreen;
  }
  return data;
}

}  // namespace keyboard

#endif
