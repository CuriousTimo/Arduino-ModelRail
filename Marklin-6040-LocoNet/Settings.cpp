#include "Settings.h"

#include <EEPROM.h>

#include "Config.h"

namespace settings {

namespace {

// EEPROM layout, version 3:
//   0  'M' '6'      marks the EEPROM as ours
//   2  version
//   3  options, one bit each
//   4  keyboards with their own assignment, one bit each (uint16_t)
//   6  coil time in 50 ms steps
//   8  fingerprint of the KeyMap.h last loaded (uint32_t)
//  16  key pair entries: 16 keyboards × 16 key pairs × uint16_t (512 bytes)
const uint8_t kMagic0 = 'M';
const uint8_t kMagic1 = '6';
const uint8_t kVersion = 3;
const int kMagicAt = 0;
const int kVersionAt = 2;
const int kOptionsAt = 3;
const int kOwnAt = 4;
const int kCoilTimeAt = 6;
const int kKeyMapStampAt = 8;
const int kAddressesAt = 16;

const uint8_t kAutoAssignBit = 0x01;
const uint8_t kStopGoBit = 0x02;
const uint8_t kHoldBit = 0x04;
const uint8_t kLogBit = 0x08;
const uint8_t kWatchdogBit = 0x10;

Options current;
uint16_t own = 0;

int addressAt(uint8_t keyboard, uint8_t keyPair) {
  return kAddressesAt + ((keyboard & 0x0F) * 16 + (keyPair & 0x0F)) * 2;
}

}  // namespace

void load() {
  if (EEPROM.read(kMagicAt) != kMagic0 || EEPROM.read(kMagicAt + 1) != kMagic1 ||
      EEPROM.read(kVersionAt) != kVersion) {
    factoryReset();
    return;
  }
  uint8_t bits = EEPROM.read(kOptionsAt);
  current.autoAssign = bits & kAutoAssignBit;
  current.stopGo = bits & kStopGoBit;
  current.holdWithoutPower = bits & kHoldBit;
  current.log = bits & kLogBit;
  current.watchdog = bits & kWatchdogBit;
  current.coilTime = EEPROM.read(kCoilTimeAt);
  EEPROM.get(kOwnAt, own);
}

void factoryReset() {
  current.autoAssign = kFactoryAutoAssign;
  current.stopGo = kFactoryStopGo;
  current.holdWithoutPower = kFactoryHoldWithoutPower;
  current.log = kFactoryLog;
  current.watchdog = kFactoryWatchdog;
  current.coilTime = kFactoryCoilTimeMs / kCoilTimeStep;
  own = 0;
  EEPROM.update(kMagicAt, kMagic0);
  EEPROM.update(kMagicAt + 1, kMagic1);
  EEPROM.update(kVersionAt, kVersion);
  EEPROM.put(kOwnAt, own);
  setKeyMapStamp(0);
  saveOptions();
}

Options& options() { return current; }

void saveOptions() {
  uint8_t bits = 0;
  if (current.autoAssign) bits |= kAutoAssignBit;
  if (current.stopGo) bits |= kStopGoBit;
  if (current.holdWithoutPower) bits |= kHoldBit;
  if (current.log) bits |= kLogBit;
  if (current.watchdog) bits |= kWatchdogBit;
  EEPROM.update(kOptionsAt, bits);
  EEPROM.update(kCoilTimeAt, current.coilTime);
}

bool hasOwnAssignment(uint8_t keyboard) { return own & (1u << (keyboard & 0x0F)); }

uint16_t autoAddressOf(uint8_t keyboard, uint8_t keyPair) {
  return (keyboard & 0x0F) * 16 + (keyPair & 0x0F) + 1;
}

uint16_t addressOf(uint8_t keyboard, uint8_t keyPair) {
  if (hasOwnAssignment(keyboard)) {
    uint16_t address = 0;
    EEPROM.get(addressAt(keyboard, keyPair), address);
    uint16_t last = isSensor(address) ? kLastSensor : kLastAddress;
    return addressPart(address) <= last ? address : 0;
  }
  return current.autoAssign ? autoAddressOf(keyboard, keyPair) : 0;
}

void setAddress(uint8_t keyboard, uint8_t keyPair, uint16_t address) {
  if (!hasOwnAssignment(keyboard)) {
    for (uint8_t pair = 0; pair < 16; pair++) {
      uint16_t was = current.autoAssign ? autoAddressOf(keyboard, pair) : 0;
      EEPROM.put(addressAt(keyboard, pair), was);
    }
    own |= 1u << (keyboard & 0x0F);
    EEPROM.put(kOwnAt, own);
  }
  EEPROM.put(addressAt(keyboard, keyPair), address);
}

void clearAssignment(uint8_t keyboard) {
  own &= ~(1u << (keyboard & 0x0F));
  EEPROM.put(kOwnAt, own);
}

uint32_t keyMapStamp() {
  uint32_t stamp = 0;
  EEPROM.get(kKeyMapStampAt, stamp);
  return stamp;
}

void setKeyMapStamp(uint32_t stamp) { EEPROM.put(kKeyMapStampAt, stamp); }

}  // namespace settings
