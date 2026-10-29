// Host tests for Keyboard.h, without an Arduino:
//   c++ -std=c++11 -Wall -Wextra -I.. keyboard_test.cpp -o keyboard_test && ./keyboard_test

#include <cstdio>

#include "Keyboard.h"

static int failures = 0;

#define CHECK(condition)                                         \
  do {                                                           \
    if (!(condition)) {                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                \
    }                                                            \
  } while (0)

using namespace keyboard;

// Keyboard 0, key 1 red, as recorded on the bus: 0xFE 0x20 0x08, answered 0x20 0xFE 0x08.
static void recordedKeyPress() {
  const uint8_t sender = 0x20, data = 0x08;
  CHECK(isKeyboard(sender));
  CHECK(keyboardOf(sender) == 0);
  CHECK(isPressed(data));
  CHECK(!isGreen(data));
  CHECK(turnoutOf(0, data) == 0);
  CHECK(i2cAddressOf(0) << 1 == 0x20);  // the answer's receiver byte
  CHECK(kCentralSender == 0xFE);
}

// Keyboard 1, decoder 2, output 3, green: key pair 12 of keyboard 1 is turnout 28 (0-based 27).
static void keyPressOnASecondKeyboard() {
  const uint8_t sender = 0x22, data = 0x2F;
  CHECK(keyboardOf(sender) == 1);
  CHECK(decoderOf(data) == 2);
  CHECK(turnoutOf(1, data) == 27);
  CHECK((turnoutOf(1, data) & 0x0F) == 11);  // key pair 12, counted from 1
  CHECK(isGreen(data));
  CHECK(dataFor(27, true, true) == data);
}

// A release names only the decoder: 0x20 for keyboard 1's decoder 2.
static void releaseNamesTheDecoder() {
  CHECK(!isPressed(0x20));
  CHECK(decoderIndexOf(1, 0x20) == 6);
  CHECK(decoderIndexOf(1, 0x2F) == 6);  // the press on that decoder
  CHECK(dataFor(27, true, false) == 0x20);
}

static void locoMessagesAreNotKeyboards() {
  CHECK(!isKeyboard(0x00));  // loco
  CHECK(!isKeyboard(0x40));  // function
  CHECK(isKeyboard(0x3E));   // keyboard 15
  CHECK(keyboardOf(0x3E) == 15);
  CHECK(i2cAddressOf(15) == 0x1F);
}

// Every key pair on every keyboard survives the round trip through the data byte.
static void everyKeyRoundTrips() {
  for (unsigned key = 0; key < kTurnouts; key++) {
    for (int green = 0; green < 2; green++) {
      const uint8_t data = dataFor(key, green, true);
      const uint8_t number = keyboardOfTurnout(key);
      CHECK(turnoutOf(number, data) == key);
      CHECK(isGreen(data) == (green != 0));
      CHECK(isPressed(data));
      CHECK(decoderIndexOf(number, dataFor(key, green, false)) == key >> 2);
    }
  }
}

int main() {
  recordedKeyPress();
  keyPressOnASecondKeyboard();
  releaseNamesTheDecoder();
  locoMessagesAreNotKeyboards();
  everyKeyRoundTrips();
  if (failures == 0) {
    std::printf("all keyboard tests passed\n");
  }
  return failures == 0 ? 0 : 1;
}
