// Märklin Keyboard 6040 on LocoNet: an Arduino Nano takes the place of the Märklin
// central on the Keyboards' I2C bus and turns their keys into LocoNet switch commands,
// like an Uhlenbrock IB-Switch. Switch commands from other LocoNet devices (Intellibox,
// Rocrail, throttles) set the Keyboards' lights.
//
// Hardware: the Arduino Nano LocoNet/DCC Interface v3 with the Märklin I2C Expansion
// Board v1 on its EXP2 ribbon cable. Pins: Config.h. The key assignment and the options
// are set on the USB serial terminal (115200 baud, type `help`) and kept in EEPROM.
// Libraries: LocoNet (mrrwa, 1.1.14 or later), Wire and EEPROM.

#include <LocoNet.h>
#include <Wire.h>
#include <avr/wdt.h>

#include "Config.h"
#include "KeyMap.h"
#include "Keyboard.h"
#include "Settings.h"
#include "SwitchQueue.h"

// Why the Nano started, saved before anything else runs; a watchdog restart leaves the
// watchdog running, so it is switched off here first.
uint8_t resetCause __attribute__((section(".noinit")));
void saveResetCause() __attribute__((naked, used, section(".init3")));
void saveResetCause() {
  resetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

namespace {

// ---------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------

enum class TrackPower : uint8_t { Unknown, On, Off };
TrackPower trackPower = TrackPower::Unknown;

// The lights of every key pair, as last set: one bit per key pair 0 to 255.
uint8_t lightKnown[keyboard::kTurnouts / 8];
uint8_t lightGreen[keyboard::kTurnouts / 8];

bool isSet(const uint8_t* bits, uint8_t key) { return bits[key >> 3] & (1 << (key & 7)); }

void setFlag(uint8_t* bits, uint8_t key, bool on) {
  if (on) {
    bits[key >> 3] |= 1 << (key & 7);
  } else {
    bits[key >> 3] &= ~(1 << (key & 7));
  }
}

// The key last pressed on each of the 64 decoders, whose coil a release switches off:
// the release only names the decoder.
struct Pressed {
  uint8_t key;
  bool green;
  bool sent;
};
Pressed pressed[keyboard::kKeyboards * 4];

// Coil-off commands waiting for the coil time to pass.
struct CoilOff {
  uint16_t address;
  bool green;
  bool waiting;
  uint32_t due;
};
const uint8_t kCoilOffSlots = 8;
CoilOff coilOffs[kCoilOffSlots];

using switchqueue::Command;

// The last coil-on this node sent: a LONG_ACK refusal right after it is its answer.
Command lastSent;
bool lastSentValid = false;
bool lastSentWasRetry = false;
uint32_t lastSentAt = 0;
const uint16_t kRefusalWindowMs = 250;

// Syncing the lights with the command station's turnout positions (OPC_SW_STATE), as an
// IB-Switch does at power-up. Its answer, a LONG_ACK, does not name the turnout: it
// belongs to the question just before it on the bus, ours or another device's.
uint8_t synced[keyboard::kTurnouts / 8];  // key pairs whose turnout was asked this round
bool syncActive = false;
bool syncWaiting = false;     // our question is out and not yet answered
bool syncUnanswered = false;  // the last question got no answer: no command station yet
uint16_t syncAsked = 0;       // the turnout of our question
uint32_t syncDue = 0;         // when to ask, or when to stop waiting for the answer
uint16_t questionAddress = 0;  // the last OPC_SW_STATE on the bus, from any device
uint32_t questionAt = 0;
// LONG_ACK to OPC_SW_STATE: this bit of its ACK1 is set for closed (0x30), clear for
// thrown (0x50), as the Intellibox answers and JMRI reads it.
const uint8_t kSwStateClosed = 0x20;

// Refused commands waiting to be sent again, oldest first, as an IB-Switch keeps them.
switchqueue::Queue refused;
uint32_t retryDue = 0;  // when to send the oldest again

/// Whether a time from millis() has come; safe across its overflow.
bool reached(uint32_t due) { return (int32_t)(millis() - due) >= 0; }

// Keyboard requests, filled by the I2C receive interrupt.
const uint8_t kRequestSlots = 8;
volatile uint8_t requestSender[kRequestSlots];
volatile uint8_t requestData[kRequestSlots];
volatile uint8_t requestHead = 0;
volatile uint8_t requestTail = 0;
volatile uint8_t requestsLost = 0;

// Answers and light updates for the keyboards, sent from loop().
struct Answer {
  uint8_t keyboard;
  uint8_t data;
  uint8_t tries;
};
const uint8_t kAnswerSlots = 16;
const uint8_t kAnswerTries = 5;
Answer answers[kAnswerSlots];
uint8_t answerHead = 0;
uint8_t answerTail = 0;
uint32_t answersDropped = 0;

uint32_t activityUntil = 0;

bool logging() { return settings::options().log; }

// The keyboards on the bus, one bit per DIP switch number: found by the scan at
// startup, or by a key they sent.
uint16_t keyboardsPresent = 0;

bool isPresent(uint8_t keyboardNumber) { return keyboardsPresent & (1u << keyboardNumber); }

/// Keyboards are numbered as Märklin does, 1 to 16 (its switch table, book and Central
/// Station); inside, a keyboard is its index 0 to 15, the number its I2C address carries.
/// Every keyboard number shown or typed goes through these two.
uint8_t shown(uint8_t index) { return index + 1; }
const uint8_t kFirstShown = 1;
const uint8_t kLastShown = keyboard::kKeyboards;

// ---------------------------------------------------------------------------------------
// Key assignment
// ---------------------------------------------------------------------------------------

using settings::addressOf;

/// Whether this firmware sends lights to a keyboard: one with its own assignment (even
/// if the scan missed it), or one found on the bus with the automatic assignment.
bool servesKeyboard(uint8_t keyboardNumber) {
  return settings::hasOwnAssignment(keyboardNumber) ||
         (settings::options().autoAssign && isPresent(keyboardNumber));
}

void startSync(uint16_t delayMs);

/// Forgets the lights of a keyboard whose assignment changed, and asks the command
/// station for its turnouts again.
void forgetLights(uint8_t keyboardNumber) {
  for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
    setFlag(lightKnown, (keyboardNumber << 4) | keyPair, false);
  }
  startSync(0);
}

void printKeyboards(uint16_t mask) {
  bool any = false;
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    if (mask & (1u << number)) {
      Serial.print(' ');
      Serial.print(shown(number));
      any = true;
    }
  }
  if (!any) {
    Serial.print(F(" none"));
  }
  Serial.println();
}

/// Finds the keyboards on the bus: each one acknowledges its own I2C address, 0x10 plus
/// its DIP switch number, where the central's answers reach it.
void scanKeyboards() {
  keyboardsPresent = 0;
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    Wire.beginTransmission(keyboard::i2cAddressOf(number));
    if (Wire.endTransmission() == 0) {
      keyboardsPresent |= 1u << number;
    }
  }
  Serial.print(F("Keyboards found:"));
  printKeyboards(keyboardsPresent);
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    bool own = settings::hasOwnAssignment(number);
    if (own && !isPresent(number)) {
      Serial.print(F("  keyboard "));
      Serial.print(shown(number));
      Serial.println(F(" has an assignment but did not answer; it still works once it sends a key"));
    } else if (!own && isPresent(number)) {
      Serial.print(F("  keyboard "));
      Serial.print(shown(number));
      if (settings::options().autoAssign) {
        Serial.print(F(": switches "));
        Serial.print(settings::autoAddressOf(number, 0));
        Serial.print(F(" to "));
        Serial.println(settings::autoAddressOf(number, 15));
      } else {
        Serial.println(F(": no assignment, its keys switch nothing (see `set`)"));
      }
    }
  }
  startSync(0);
}

// ---------------------------------------------------------------------------------------
// Key map file (KeyMap.h)
// ---------------------------------------------------------------------------------------

const uint8_t kKeyMapLines = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

/// A fingerprint of KeyMap.h's table (FNV-1a), never 0.
uint32_t keyMapStampOfFile() {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(kKeyMap);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(kKeyMap); i++) {
    hash = (hash ^ pgm_read_byte(bytes + i)) * 16777619UL;
  }
  return hash == 0 ? 1 : hash;
}

/// A key pair entry from the file, or 0 with a message if it is not valid.
uint16_t checkedKeyMapEntry(uint8_t number, uint8_t keyPair, uint16_t entry) {
  uint16_t address = settings::addressPart(entry);
  bool sensor = settings::isSensor(entry);
  bool valid = entry == 0 || (address >= 1 && address <= (sensor ? settings::kLastSensor
                                                                   : settings::kLastAddress));
  if (valid) {
    return entry;
  }
  Serial.print(F("KeyMap.h: keyboard "));
  Serial.print(shown(number));
  Serial.print(F(" key pair "));
  Serial.print(keyPair + 1);
  Serial.println(sensor ? F(": track sections are 1 to 4096; this key pair does nothing")
                        : F(": turnouts are 1 to 2048; this key pair does nothing"));
  return 0;
}

/// Copies KeyMap.h into the Nano's memory.
void loadKeyMap() {
  uint16_t loaded = 0;
  for (uint8_t line = 0; line < kKeyMapLines; line++) {
    uint8_t written = pgm_read_byte(&kKeyMap[line].keyboard);  // Märklin's 1 to 16
    if (written < kFirstShown || written > kLastShown) {
      Serial.print(F("KeyMap.h: keyboard "));
      Serial.print(written);
      Serial.println(F(" does not exist, Keyboards are 1 to 16; this line is skipped"));
      continue;
    }
    uint8_t number = written - 1;
    if (loaded & (1u << number)) {
      Serial.print(F("KeyMap.h: keyboard "));
      Serial.print(written);
      Serial.println(F(" is listed twice; the last line counts"));
    }
    for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
      uint16_t entry = pgm_read_word(&kKeyMap[line].keys[keyPair]);
      settings::setAddress(number, keyPair, checkedKeyMapEntry(number, keyPair, entry));
    }
    forgetLights(number);
    loaded |= 1u << number;
  }
  settings::setKeyMapStamp(keyMapStampOfFile());
  Serial.print(F("Key map from KeyMap.h loaded for keyboards:"));
  printKeyboards(loaded);
}

/// At startup: loads KeyMap.h if it is in use and changed since it was last loaded.
void loadKeyMapIfChanged() {
  if (kUseKeyMap && settings::keyMapStamp() != keyMapStampOfFile()) {
    loadKeyMap();
  }
}

// ---------------------------------------------------------------------------------------
// Keyboard side (I2C)
// ---------------------------------------------------------------------------------------

/// I2C receive interrupt: a keyboard sends its sender byte and its data byte.
void onKeyboardMessage(int count) {
  if (count != 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return;
  }
  uint8_t sender = Wire.read();
  uint8_t data = Wire.read();
  uint8_t next = (requestHead + 1) % kRequestSlots;
  if (next == requestTail) {
    requestsLost++;  // the keyboard repeats it
    return;
  }
  requestSender[requestHead] = sender;
  requestData[requestHead] = data;
  requestHead = next;
}

bool nextRequest(uint8_t& sender, uint8_t& data) {
  noInterrupts();
  bool available = requestTail != requestHead;
  if (available) {
    sender = requestSender[requestTail];
    data = requestData[requestTail];
    requestTail = (requestTail + 1) % kRequestSlots;
  }
  interrupts();
  return available;
}

/// Queues a message to a keyboard: the answer to its request, or new lights.
void sendToKeyboard(uint8_t keyboardNumber, uint8_t data) {
  uint8_t next = (answerHead + 1) % kAnswerSlots;
  if (next == answerTail) {
    answersDropped++;
    return;
  }
  answers[answerHead] = {keyboardNumber, data, 0};
  answerHead = next;
}

/// Sends the oldest queued message. The I2C bus has several masters, so a send can
/// lose arbitration to a keyboard; it is tried again on the next loop.
void flushKeyboardMessages() {
  if (answerTail == answerHead) {
    return;
  }
  Answer& answer = answers[answerTail];
  Wire.beginTransmission(keyboard::i2cAddressOf(answer.keyboard));
  Wire.write(keyboard::kCentralSender);
  Wire.write(answer.data);
  uint8_t result = Wire.endTransmission();
  if (result == 0 || ++answer.tries >= kAnswerTries) {
    if (result != 0) {
      answersDropped++;
      if (logging()) {
        Serial.print(F("I2C: keyboard "));
        Serial.print(shown(answer.keyboard));
        Serial.print(F(" did not take 0x"));
        Serial.print(answer.data, HEX);
        Serial.print(F(" (error "));
        Serial.print(result);
        Serial.println(F(")"));
      }
    }
    answerTail = (answerTail + 1) % kAnswerSlots;
  }
}

void setLight(uint8_t key, bool green) {
  setFlag(lightKnown, key, true);
  setFlag(lightGreen, key, green);
}

/// The answer that leaves a key's lights as they were, or the key's own request when
/// its position is not known.
uint8_t unchangedAnswer(uint8_t key, uint8_t request) {
  if (!isSet(lightKnown, key)) {
    return request;
  }
  return keyboard::dataFor(key, isSet(lightGreen, key), true);
}

void logSwitch(const __FlashStringHelper* what, uint16_t address, bool green) {
  if (!logging()) {
    return;
  }
  Serial.print(what);
  Serial.print(address);
  Serial.println(green ? F(" closed") : F(" thrown"));
}

/// Schedules the coil-off for a coil-on just sent, `coilTime` ms later. A newer coil-on for
/// the same turnout replaces the waiting one; when every slot is taken, the oldest is sent
/// early.
void scheduleCoilOff(uint16_t address, bool green, uint32_t coilTime) {
  uint8_t slot = kCoilOffSlots;
  uint8_t earliest = 0;
  for (uint8_t i = 0; i < kCoilOffSlots; i++) {
    if (coilOffs[i].waiting && coilOffs[i].address == address) {
      slot = i;
      break;
    }
    if (!coilOffs[i].waiting && slot == kCoilOffSlots) {
      slot = i;
    }
    if ((int32_t)(coilOffs[i].due - coilOffs[earliest].due) < 0) {
      earliest = i;
    }
  }
  if (slot == kCoilOffSlots) {
    slot = earliest;
    LocoNet.requestSwitch(coilOffs[slot].address, 0, coilOffs[slot].green ? 1 : 0);
  }
  coilOffs[slot] = {address, green, true, millis() + coilTime};
}

void cancelCoilOff(uint16_t address) {
  for (uint8_t i = 0; i < kCoilOffSlots; i++) {
    if (coilOffs[i].waiting && coilOffs[i].address == address) {
      coilOffs[i].waiting = false;
    }
  }
}

/// Sends the coil-offs whose time has come.
void handleCoilOffs() {
  for (uint8_t i = 0; i < kCoilOffSlots; i++) {
    if (coilOffs[i].waiting && reached(coilOffs[i].due)) {
      coilOffs[i].waiting = false;
      LocoNet.requestSwitch(coilOffs[i].address, 0, coilOffs[i].green ? 1 : 0);
    }
  }
}

/// Sends a coil-on and schedules its coil-off: after the coil time, or for a command sent
/// again (its key was released long ago) after the factory coil time when the coil
/// goes off on release.
LN_STATUS switchOn(const Command& command, bool retry) {
  // LocoNet's direction bit set is closed/green (LocoNet PE, JMRI); the LocoNet
  // library's header comment has it the other way round.
  LN_STATUS status = LocoNet.requestSwitch(command.address, 1, command.green ? 1 : 0);
  if (status == LN_DONE) {
    lastSent = command;
    lastSentValid = true;
    lastSentWasRetry = retry;
    lastSentAt = millis();
    uint32_t coilTime = (uint32_t)settings::options().coilTime * settings::kCoilTimeStep;
    if (coilTime == 0 && retry) {
      coilTime = kFactoryCoilTimeMs;
    }
    if (coilTime > 0) {
      scheduleCoilOff(command.address, command.green, coilTime);
    }
  }
  return status;
}

/// Drops a refused command waiting for this turnout: a newer key press or another device
/// switched it since.
void cancelRetry(uint16_t address) { refused.remove(address); }

/// The command station refused a switch command (LONG_ACK 0 for OPC_SW_REQ): it is in STOP
/// or its buffer is full. Ours, if we sent one just before. The command was on LocoNet all
/// the same, so a decoder on LocoNet or with its own power has switched; the lights keep
/// what was pressed. The command waits to be sent again, for the decoders on the track.
void handleRefusal() {
  if (!lastSentValid || millis() - lastSentAt > kRefusalWindowMs) {
    return;  // another device's command
  }
  lastSentValid = false;
  Command command = lastSent;
  cancelCoilOff(command.address);
  // A command sent again keeps its place; a new one waits behind the others.
  bool kept = lastSentWasRetry ? refused.pushFront(command) : refused.pushBack(command);
  retryDue = millis() + kRetryAfterMs;
  if (!kept && logging()) {
    Serial.println(F("REFUSED: more than 16 commands waiting; one is dropped"));
  }
  if (!lastSentWasRetry) {
    logSwitch(F("REFUSED: sent again every 3 s and at track power on: switch "), command.address,
              command.green);
  }
}

/// Sends the oldest refused command again: every 3 s while the command station refuses,
/// and one after another once it takes them (after track power on at once).
void handleRetries() {
  if (refused.empty() || !reached(retryDue)) {
    return;
  }
  if (lastSentValid && millis() - lastSentAt <= kRefusalWindowMs) {
    return;  // the answer to the last command may still come
  }
  Command command = refused.popFront();
  if (switchOn(command, true) != LN_DONE) {
    refused.pushFront(command);  // LocoNet is busy: shortly
    retryDue = millis() + kRefusalWindowMs;
    return;
  }
  logSwitch(F("RETRY: switch "), command.address, command.green);
  // The next one once this one was not refused.
  retryDue = millis() + kRefusalWindowMs + 1;
}

void logKey(uint8_t keyboardNumber, uint8_t keyPair, bool green, uint16_t address) {
  Serial.print(F("KEY: keyboard "));
  Serial.print(shown(keyboardNumber));
  Serial.print(F(" key "));
  Serial.print(keyPair + 1);
  Serial.print(green ? F(" green") : F(" red"));
  if (address == 0) {
    Serial.print(F(", not assigned"));
  } else if (settings::isSensor(address)) {
    Serial.print(F(" shows sensor "));
    Serial.print(settings::addressPart(address));
  } else {
    Serial.print(F(" -> LocoNet switch "));
    Serial.print(address);
    Serial.print(green ? F(" closed") : F(" thrown"));
  }
}

void handleKeyPress(uint8_t keyboardNumber, uint8_t data) {
  uint8_t key = keyboard::turnoutOf(keyboardNumber, data);
  uint8_t keyPair = key & 0x0F;
  bool green = keyboard::isGreen(data);
  uint16_t address = addressOf(keyboardNumber, keyPair);
  if (logging()) {
    logKey(keyboardNumber, keyPair, green, address);
  }
  if (address == 0) {
    if (logging()) {
      Serial.println();
    }
    sendToKeyboard(keyboardNumber, data);
    return;
  }
  if (settings::isSensor(address)) {
    // A sensor key switches nothing: its lights keep showing the sensor.
    if (logging()) {
      Serial.println(F(", not sent"));
    }
    sendToKeyboard(keyboardNumber, unchangedAnswer(key, data));
    return;
  }
  if (settings::options().holdWithoutPower && trackPower == TrackPower::Off) {
    if (logging()) {
      Serial.println(F(": not sent, track power is off"));
    }
    sendToKeyboard(keyboardNumber, unchangedAnswer(key, data));
    return;
  }
  cancelRetry(address);  // a new key press replaces a waiting retry
  LN_STATUS status = switchOn({address, green}, false);
  if (logging()) {
    Serial.print(F(": "));
    Serial.println(LocoNet.getStatusStr(status));
  }
  if (status == LN_DONE) {
    setLight(key, green);
    pressed[keyboard::decoderIndexOf(keyboardNumber, data)] = {key, green, true};
    sendToKeyboard(keyboardNumber, data);
  } else if (isSet(lightKnown, key)) {
    sendToKeyboard(keyboardNumber, unchangedAnswer(key, data));
  }
  // Otherwise no answer: the keyboard asks again in about a second.
}

void handleKeyRelease(uint8_t keyboardNumber, uint8_t data) {
  Pressed& last = pressed[keyboard::decoderIndexOf(keyboardNumber, data)];
  // With a coil time the coil-off is already scheduled; without one, the release sends it.
  if (last.sent && settings::options().coilTime > 0) {
    last.sent = false;
  }
  if (last.sent) {
    last.sent = false;
    uint16_t address = addressOf(keyboardNumber, last.key & 0x0F);
    if (address != 0 && !settings::isSensor(address)) {
      LocoNet.requestSwitch(address, 0, last.green ? 1 : 0);
    }
  }
  sendToKeyboard(keyboardNumber, data);
}

void handleKeyboardRequests() {
  uint8_t sender = 0;
  uint8_t data = 0;
  while (nextRequest(sender, data)) {
    if (!keyboard::isKeyboard(sender)) {
      continue;  // a loco control, not a keyboard
    }
    activityUntil = millis() + 50;
    uint8_t keyboardNumber = keyboard::keyboardOf(sender);
    if (!isPresent(keyboardNumber)) {
      keyboardsPresent |= 1u << keyboardNumber;
      Serial.print(F("Keyboard "));
      Serial.print(shown(keyboardNumber));
      Serial.println(F(" found: it sent a key"));
      startSync(0);
    }
    if (keyboard::isPressed(data)) {
      handleKeyPress(keyboardNumber, data);
    } else {
      handleKeyRelease(keyboardNumber, data);
    }
  }
}

// ---------------------------------------------------------------------------------------
// LocoNet side
// ---------------------------------------------------------------------------------------

/// Sets the lights of every key pair with this entry (a turnout, or a sensor with
/// kSensorFlag). Returns how many key pairs changed.
uint8_t showEntry(uint16_t entry, bool green) {
  uint8_t changed = 0;
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    if (!servesKeyboard(number)) {
      continue;
    }
    for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
      if (addressOf(number, keyPair) != entry) {
        continue;
      }
      uint8_t key = (number << 4) | keyPair;
      // Our own commands come back from the bus; only a change is sent.
      if (isSet(lightKnown, key) && isSet(lightGreen, key) == green) {
        continue;
      }
      setLight(key, green);
      sendToKeyboard(number, keyboard::dataFor(key, green, true));
      activityUntil = millis() + 50;
      changed++;
      if (logging()) {
        if (settings::isSensor(entry)) {
          Serial.print(F("LN: sensor "));
          Serial.print(settings::addressPart(entry));
          Serial.print(green ? F(" free") : F(" occupied"));
        } else {
          Serial.print(F("LN: switch "));
          Serial.print(entry);
          Serial.print(green ? F(" closed") : F(" thrown"));
        }
        Serial.print(F(" -> keyboard "));
        Serial.print(shown(number));
        Serial.print(F(" key "));
        Serial.println(keyPair + 1);
      }
    }
  }
  return changed;
}

/// A turnout's position from LocoNet: green closed, red thrown.
void showTurnout(uint16_t address, bool green) { showEntry(address, green); }

/// An occupancy detector's state from LocoNet: red occupied, green free.
void showSensor(uint16_t address, bool occupied) {
  showEntry(settings::kSensorFlag | address, !occupied);
}

/// Starts asking the command station for every assigned turnout's position, after
/// `delayMs`. Called at startup, after track power on and when an assignment changes.
void startSync(uint16_t delayMs) {
  memset(synced, 0, sizeof(synced));
  syncActive = true;
  syncWaiting = false;
  syncDue = millis() + delayMs;
}

/// Marks every key pair of this turnout as asked in this round.
void markSynced(uint16_t address) {
  for (uint16_t key = 0; key < keyboard::kTurnouts; key++) {
    if (addressOf(key >> 4, key & 0x0F) == address) {
      setFlag(synced, key, true);
    }
  }
}

/// The next key pair whose turnout is still to be asked, or -1 when all are done.
int16_t nextToSync() {
  for (uint16_t key = 0; key < keyboard::kTurnouts; key++) {
    uint8_t number = key >> 4;
    uint16_t entry = addressOf(number, key & 0x0F);
    if (entry != 0 && !settings::isSensor(entry) && servesKeyboard(number) &&
        !isSet(synced, key)) {
      return key;
    }
  }
  return -1;
}

/// Asks the next turnout, one at a time; without an answer, asks the same one again after
/// 3 s, as an IB-Switch does with no command station on the bus.
void handleSync() {
  if (!syncActive || !reached(syncDue)) {
    return;
  }
  if (syncWaiting) {
    syncWaiting = false;
    if (!syncUnanswered && logging()) {
      Serial.println(F("SYNC: no command station answers; asking again every 3 s"));
    }
    syncUnanswered = true;
    syncDue = millis() + kRetryAfterMs;
    return;
  }
  int16_t key = nextToSync();
  if (key < 0) {
    syncActive = false;
    if (logging()) {
      Serial.println(F("SYNC: the lights show the command station's turnout positions"));
    }
    return;
  }
  uint16_t address = addressOf(key >> 4, key & 0x0F);
  if (LocoNet.reportSwitch(address) != LN_DONE) {
    syncDue = millis() + kSyncAnswerMs;  // the bus is busy or gone: try again shortly
    return;
  }
  syncAsked = address;
  questionAddress = address;
  questionAt = millis();
  syncWaiting = true;
  syncDue = millis() + kSyncAnswerMs;
}

/// A turnout position from a LONG_ACK to OPC_SW_STATE: the answer to the question just
/// before it, whether this firmware, an IB-Switch or the Intellibox's keyboard asked.
void handleSwitchState(uint8_t ack1) {
  if (questionAddress == 0 || millis() - questionAt > kSyncAnswerMs) {
    return;
  }
  uint16_t address = questionAddress;
  questionAddress = 0;
  showTurnout(address, ack1 & kSwStateClosed);
  if (syncActive) {
    markSynced(address);
  }
  if (syncWaiting && address == syncAsked) {
    syncWaiting = false;
    syncUnanswered = false;
    syncDue = millis();
  }
}

void handleLocoNet() {
  lnMsg* packet = LocoNet.receive();
  if (packet == nullptr) {
    return;
  }
  if (packet->data[0] == OPC_LONG_ACK && packet->data[1] == (OPC_SW_REQ & 0x7F) &&
      packet->data[2] == 0) {
    handleRefusal();
  }
  LocoNet.processSwitchSensorMessage(packet);
}

// ---------------------------------------------------------------------------------------
// Märklin STOP and GO lines (optional: `option stopgo on` on the terminal)
// ---------------------------------------------------------------------------------------

struct EdgeInput {
  uint8_t pin;
  bool level;
  uint32_t lockedUntil;

  /// A rising edge, at most one per 100 ms.
  bool rose() {
    bool now = digitalRead(pin) == HIGH;
    bool edge = now && !level && millis() >= lockedUntil;
    if (now != level) {
      level = now;
      if (edge) {
        lockedUntil = millis() + 100;
      }
    }
    return edge;
  }
};
EdgeInput stopLine = {kStopPin, false, 0};
EdgeInput goLine = {kGoPin, false, 0};

void handleStopGo() {
  if (!settings::options().stopGo) {
    return;
  }
  if (stopLine.rose()) {
    LocoNet.reportPower(0);
    trackPower = TrackPower::Off;
    Serial.println(F("STOP on the Märklin bus -> LocoNet power off"));
  }
  if (goLine.rose()) {
    LocoNet.reportPower(1);
    trackPower = TrackPower::On;
    Serial.println(F("GO on the Märklin bus -> LocoNet power on"));
  }
}

// ---------------------------------------------------------------------------------------
// Status LED and serial console
// ---------------------------------------------------------------------------------------

/// On: track power on (or not yet known). Slow blink: power off. Short dark flicker:
/// a key or a LocoNet switch command.
void updateLed() {
  uint32_t now = millis();
  bool on = trackPower == TrackPower::Off ? (now / 500) % 2 == 0 : true;
  if (now < activityUntil) {
    on = !on;
  }
  digitalWrite(kStatusLedPin, on ? HIGH : LOW);
}

// ---------------------------------------------------------------------------------------
// Serial terminal
// ---------------------------------------------------------------------------------------

void printHelp() {
  Serial.println(F("Commands (keyboards 1-16 as in Märklin's switch table, key pairs 1-16):"));
  Serial.println(F("  status                    track power, keyboards, counters"));
  Serial.println(F("  scan                      look for keyboards on the bus"));
  Serial.println(F("  show                      every keyboard's assignment"));
  Serial.println(F("  show <kb>                 one keyboard's 16 key pairs"));
  Serial.println(F("  set <kb> <key> <address>  key pair switches LocoNet address 1-2048 (0: nothing)"));
  Serial.println(F("  set <kb> all <first>      key pairs 1-16 switch first to first+15"));
  Serial.println(F("  sensor <kb> <key> <addr>  key pair shows LocoNet sensor 1-4096: red occupied, green free"));
  Serial.println(F("  sensor <kb> all <first>   key pairs 1-16 show sensors first to first+15"));
  Serial.println(F("  clear <kb>                keyboard back to the automatic assignment"));
  Serial.println(F("  option                    show the options"));
  Serial.println(F("  option <name> on|off      auto, stopgo, hold, log, watchdog"));
  Serial.println(F("  option coil <ms>          how long a coil stays on (0: until the key is released)"));
  Serial.println(F("  version                   the firmware version"));
  Serial.println(F("  keymap                    whether KeyMap.h is used; `keymap load` loads it again"));
  Serial.println(F("  backup                    the whole setup as commands: keep it to restore later"));
  Serial.println(F("  keys <kb> <16 entries>    all key pairs at once: turnout, 0, or s and a sensor"));
  Serial.println(F("  reset yes                 factory settings: every keyboard automatic"));
}

void printVersion() {
  Serial.print(F("Marklin-6040-LocoNet "));
  Serial.println(kFirmwareVersion);
}

void printStatus() {
  Serial.print(F("Track power: "));
  Serial.println(trackPower == TrackPower::On    ? F("on")
                 : trackPower == TrackPower::Off ? F("off")
                                                 : F("not yet known"));
  uint16_t own = 0;
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    if (settings::hasOwnAssignment(number)) {
      own |= 1u << number;
    }
  }
  Serial.print(F("Keyboards on the bus:"));
  printKeyboards(keyboardsPresent);
  Serial.print(F("Keyboards with their own assignment:"));
  printKeyboards(own);
  Serial.print(F("Keyboard messages lost: "));
  Serial.print(requestsLost);
  Serial.print(F(", answers dropped: "));
  Serial.println(answersDropped);
  LnBufStats* stats = LocoNet.getStats();
  Serial.print(F("LocoNet received: "));
  Serial.print(stats->RxPackets);
  Serial.print(F(", errors: "));
  Serial.print(stats->RxErrors);
  Serial.print(F(", sent: "));
  Serial.print(stats->TxPackets);
  Serial.print(F(", collisions: "));
  Serial.println(stats->Collisions);
  Serial.print(F("Refused switch commands waiting: "));
  Serial.println(refused.size());
  Serial.print(F("Turnout positions: "));
  Serial.println(!syncActive      ? F("synced with the command station")
                 : syncUnanswered ? F("no command station answers yet; asking every 3 s")
                                  : F("asking the command station"));
}

uint8_t digitsOf(uint16_t value) {
  uint8_t digits = 1;
  for (uint16_t rest = value; rest >= 10; rest /= 10) {
    digits++;
  }
  return digits;
}

/// `value` right-aligned in `width` columns, after `prefix` if it is not 0.
void printPadded(uint16_t value, uint8_t width, char prefix = 0) {
  uint8_t used = digitsOf(value) + (prefix ? 1 : 0);
  while (used++ < width) {
    Serial.print(' ');
  }
  if (prefix) {
    Serial.print(prefix);
  }
  Serial.print(value);
}

/// One keyboard: where its assignment comes from, then a row of key pairs and a row of
/// LocoNet addresses ("s" before a sensor, "-" for none).
void printKeyboard(uint8_t number) {
  Serial.print(F("Keyboard "));
  Serial.print(shown(number));
  Serial.print(isPresent(number) ? F(", on the bus") : F(", not found"));
  if (settings::hasOwnAssignment(number)) {
    Serial.println(F(", own assignment"));
  } else if (settings::options().autoAssign) {
    Serial.println(F(", automatic assignment"));
  } else {
    Serial.println(F(", no assignment"));
  }
  Serial.print(F("  key:    "));
  for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
    printPadded(keyPair + 1, 6);
  }
  Serial.println();
  Serial.print(F("  switch: "));
  for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
    uint16_t address = addressOf(number, keyPair);
    if (address == 0) {
      Serial.print(F("     -"));
    } else if (settings::isSensor(address)) {
      printPadded(settings::addressPart(address), 6, 's');
    } else {
      printPadded(address, 6);
    }
  }
  Serial.println();
}

/// Every keyboard that is on the bus or has its own assignment.
void printAssignments() {
  bool any = false;
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    if (isPresent(number) || settings::hasOwnAssignment(number)) {
      printKeyboard(number);
      any = true;
    }
  }
  if (!any) {
    Serial.println(F("No keyboards found and none assigned. `scan` looks again; `show <kb>` shows any keyboard."));
  }
}

void printOptions() {
  const settings::Options& options = settings::options();
  Serial.print(F("  auto    "));
  Serial.print(options.autoAssign ? F("on ") : F("off"));
  Serial.println(F("  keyboards without their own assignment: keyboard 1 switches 1-16, 2 17-32 ..."));
  Serial.print(F("  stopgo  "));
  Serial.print(options.stopGo ? F("on ") : F("off"));
  Serial.println(F("  Märklin STOP/GO lines switch LocoNet power (measure the lines first)"));
  Serial.print(F("  hold    "));
  Serial.print(options.holdWithoutPower ? F("on ") : F("off"));
  Serial.println(F("  keys are not sent while track power is off"));
  Serial.print(F("  log     "));
  Serial.print(options.log ? F("on ") : F("off"));
  Serial.println(F("  log keys and switch commands"));
  Serial.print(F("  watchdog "));
  Serial.print(options.watchdog ? F("on ") : F("off"));
  Serial.println(F(" restart the Nano if it hangs for 2 s"));
  Serial.print(F("  coil    "));
  if (options.coilTime == 0) {
    Serial.println(F("0    coils switch off when the key is released"));
  } else {
    Serial.print((uint16_t)options.coilTime * settings::kCoilTimeStep);
    Serial.println(F(" ms  how long a turnout coil stays on"));
  }
}

/// A whole number from `text` within min to max, or false.
bool parseNumber(const char* text, long min, long max, long& value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  value = strtol(text, &end, 10);
  return *end == '\0' && value >= min && value <= max;
}

bool parseKeyboard(const char* text, uint8_t& number) {
  long value;
  if (!parseNumber(text, kFirstShown, kLastShown, value)) {
    Serial.println(F("Keyboards are 1 to 16, as in Märklin's switch table."));
    return false;
  }
  number = value - 1;
  return true;
}

bool parseOnOff(const char* text, bool& on) {
  if (text != nullptr && strcasecmp(text, "on") == 0) {
    on = true;
    return true;
  }
  if (text != nullptr && strcasecmp(text, "off") == 0) {
    on = false;
    return true;
  }
  Serial.println(F("Give on or off."));
  return false;
}

/// One key pair entry as `keys` and `backup` write it: a turnout number, 0 for nothing,
/// or s and a number for a sensor.
void printEntry(uint16_t entry) {
  if (settings::isSensor(entry)) {
    Serial.print('s');
  }
  Serial.print(settings::addressPart(entry));
}

bool parseEntry(const char* text, uint16_t& entry) {
  if (text == nullptr) {
    return false;
  }
  long value;
  if (text[0] == 's' || text[0] == 'S') {
    if (!parseNumber(text + 1, 1, settings::kLastSensor, value)) {
      return false;
    }
    entry = settings::kSensorFlag | value;
    return true;
  }
  if (!parseNumber(text, 0, settings::kLastAddress, value)) {
    return false;
  }
  entry = value;
  return true;
}

void printOnOff(const __FlashStringHelper* name, bool on) {
  Serial.print(F("option "));
  Serial.print(name);
  Serial.println(on ? F(" on") : F(" off"));
}

/// Prints the whole setup as the commands that recreate it, so a copy of this text is a
/// backup and sending it back line by line restores it. Lines with # are comments.
void commandBackup() {
  Serial.print(F("# Marklin-6040-LocoNet "));
  Serial.print(kFirmwareVersion);
  Serial.println(F(" backup"));
  const settings::Options& options = settings::options();
  printOnOff(F("auto"), options.autoAssign);
  printOnOff(F("stopgo"), options.stopGo);
  printOnOff(F("hold"), options.holdWithoutPower);
  printOnOff(F("log"), options.log);
  printOnOff(F("watchdog"), options.watchdog);
  Serial.print(F("option coil "));
  Serial.println((uint16_t)options.coilTime * settings::kCoilTimeStep);
  for (uint8_t number = 0; number < keyboard::kKeyboards; number++) {
    bool own = settings::hasOwnAssignment(number);
    Serial.print(F("# keyboard "));
    Serial.print(shown(number));
    Serial.print(isPresent(number) ? F(": on the bus") : F(": not found"));
    Serial.println(own ? F(", own assignment") : F(", automatic"));
    if (!own) {
      Serial.print(F("clear "));
      Serial.println(shown(number));
      continue;
    }
    Serial.print(F("keys "));
    Serial.print(shown(number));
    for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
      Serial.print(' ');
      printEntry(addressOf(number, keyPair));
    }
    Serial.println();
  }
  Serial.println(F("# end of backup"));
}

/// `keys <kb> <16 entries>`: all key pairs of a keyboard in one line. The first entry
/// comes in as `first`; strtok gives the rest.
void commandKeys(char* keyboardText, char* first, char* second) {
  uint8_t number;
  if (!parseKeyboard(keyboardText, number)) {
    return;
  }
  uint16_t entries[16];
  char* text = first;
  for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
    if (keyPair == 1) {
      text = second;
    } else if (keyPair > 1) {
      text = strtok(nullptr, " \t");
    }
    if (!parseEntry(text, entries[keyPair])) {
      Serial.print(F("Key pair "));
      Serial.print(keyPair + 1);
      Serial.println(text == nullptr ? F(" is missing: `keys` needs all 16 key pairs.")
                                     : F(": give a turnout 0 to 2048 or a sensor s1 to s4096."));
      return;
    }
  }
  if (strtok(nullptr, " \t") != nullptr) {
    Serial.println(F("More than 16 key pairs: nothing was changed."));
    return;
  }
  for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
    settings::setAddress(number, keyPair, entries[keyPair]);
  }
  forgetLights(number);
  printKeyboard(number);
}

/// `set` (turnouts, 1 to 2048) and `sensor` (sensors, 1 to 4096).
void commandSet(bool sensor, char* keyboardText, char* keyText, char* addressText) {
  uint8_t number;
  if (!parseKeyboard(keyboardText, number)) {
    return;
  }
  const long last = sensor ? settings::kLastSensor : settings::kLastAddress;
  const uint16_t flag = sensor ? settings::kSensorFlag : 0;
  long address;
  if (keyText != nullptr && strcasecmp(keyText, "all") == 0) {
    if (!parseNumber(addressText, 1, last - 15, address)) {
      Serial.print(F("Give the first address, 1 to "));
      Serial.print(last - 15);
      Serial.println(F(": `set <kb> all <first>` or `sensor <kb> all <first>`."));
      return;
    }
    for (uint8_t keyPair = 0; keyPair < 16; keyPair++) {
      settings::setAddress(number, keyPair, flag | (address + keyPair));
    }
  } else {
    long key;
    if (!parseNumber(keyText, 1, 16, key)) {
      Serial.println(F("Key pairs are 1 to 16, left to right."));
      return;
    }
    if (!parseNumber(addressText, sensor ? 1 : 0, last, address)) {
      Serial.println(sensor ? F("LocoNet sensors are 1 to 4096.")
                            : F("LocoNet addresses are 1 to 2048, or 0 for nothing."));
      return;
    }
    settings::setAddress(number, key - 1, address == 0 ? 0 : flag | address);
  }
  forgetLights(number);
  printKeyboard(number);
}

void commandOption(char* name, char* value) {
  if (name == nullptr) {
    printOptions();
    return;
  }
  settings::Options& options = settings::options();
  if (strcasecmp(name, "coil") == 0) {
    long ms;
    if (!parseNumber(value, 0, 255L * settings::kCoilTimeStep, ms)) {
      Serial.println(F("Give the coil time in ms, 0 to 12750, in steps of 50: `option coil 200`."));
      return;
    }
    options.coilTime = (ms + settings::kCoilTimeStep / 2) / settings::kCoilTimeStep;
    settings::saveOptions();
    printOptions();
    return;
  }
  bool* option = nullptr;
  if (strcasecmp(name, "auto") == 0) {
    option = &options.autoAssign;
  } else if (strcasecmp(name, "stopgo") == 0) {
    option = &options.stopGo;
  } else if (strcasecmp(name, "hold") == 0) {
    option = &options.holdWithoutPower;
  } else if (strcasecmp(name, "log") == 0) {
    option = &options.log;
  } else if (strcasecmp(name, "watchdog") == 0) {
    option = &options.watchdog;
  } else {
    Serial.println(F("Options are auto, stopgo, hold, log, watchdog and coil."));
    return;
  }
  bool on;
  if (!parseOnOff(value, on)) {
    return;
  }
  *option = on;
  settings::saveOptions();
  if (option == &options.watchdog) {
    if (on) {
      wdt_enable(WDTO_2S);
    } else {
      wdt_disable();
    }
  }
  printOptions();
}

void runCommand(char* line) {
  if (line[0] == '#') {
    return;  // a comment, as in a backup
  }
  // Echo the command: serial monitors do not show what was typed in their output.
  Serial.print(F("> "));
  Serial.println(line);
  char* command = strtok(line, " \t");
  char* first = strtok(nullptr, " \t");
  char* second = strtok(nullptr, " \t");
  char* third = strtok(nullptr, " \t");
  if (command == nullptr) {
    return;
  }
  uint8_t number;
  if (strcasecmp(command, "help") == 0 || strcasecmp(command, "?") == 0) {
    printHelp();
  } else if (strcasecmp(command, "status") == 0) {
    printStatus();
  } else if (strcasecmp(command, "version") == 0) {
    printVersion();
  } else if (strcasecmp(command, "backup") == 0) {
    commandBackup();
  } else if (strcasecmp(command, "keys") == 0) {
    commandKeys(first, second, third);
  } else if (strcasecmp(command, "keymap") == 0) {
    if (!kUseKeyMap) {
      Serial.println(F("KeyMap.h is not used: set kUseKeyMap to true in KeyMap.h and load the program again."));
    } else if (first != nullptr && strcasecmp(first, "load") == 0) {
      loadKeyMap();
    } else {
      Serial.print(F("KeyMap.h is used, "));
      Serial.print(kKeyMapLines);
      Serial.println(settings::keyMapStamp() == keyMapStampOfFile()
                         ? F(" lines, loaded. Changes made here stay until a changed KeyMap.h is loaded.")
                         : F(" lines, not loaded yet: type `keymap load`."));
    }
  } else if (strcasecmp(command, "scan") == 0) {
    scanKeyboards();
  } else if (strcasecmp(command, "show") == 0) {
    if (first == nullptr) {
      printAssignments();
    } else if (parseKeyboard(first, number)) {
      printKeyboard(number);
    }
  } else if (strcasecmp(command, "set") == 0) {
    commandSet(false, first, second, third);
  } else if (strcasecmp(command, "sensor") == 0) {
    commandSet(true, first, second, third);
  } else if (strcasecmp(command, "clear") == 0) {
    if (parseKeyboard(first, number)) {
      settings::clearAssignment(number);
      forgetLights(number);
      printKeyboard(number);
    }
  } else if (strcasecmp(command, "option") == 0) {
    commandOption(first, second);
  } else if (strcasecmp(command, "reset") == 0) {
    if (first != nullptr && strcasecmp(first, "yes") == 0) {
      settings::factoryReset();
      for (uint8_t keyboardNumber = 0; keyboardNumber < keyboard::kKeyboards; keyboardNumber++) {
        forgetLights(keyboardNumber);
      }
      Serial.println(F("Factory settings restored."));
      if (kUseKeyMap) {
        loadKeyMap();
      }
      printOptions();
    } else {
      Serial.println(F("`reset yes` removes every keyboard's own assignment and resets the options."));
    }
  } else {
    Serial.print(F("Unknown command: "));
    Serial.println(command);
    Serial.println(F("Type `help` for the commands."));
  }
}

/// Reads the terminal a line at a time. Any line ending works, and so does none: a
/// command also runs half a second after its last character, for a Serial Monitor set
/// to "No line ending". Capital letters work too.
void handleTerminal() {
  static char line[128];
  static uint8_t length = 0;
  static uint32_t lastCharAt = 0;
  while (Serial.available()) {
    char c = Serial.read();
    lastCharAt = millis();
    if (c == '\n' || c == '\r') {
      if (length > 0) {
        line[length] = '\0';
        length = 0;
        runCommand(line);
      }
    } else if (length < sizeof(line) - 1) {
      line[length++] = c;
    }
  }
  if (length > 0 && millis() - lastCharAt > 500) {
    line[length] = '\0';
    length = 0;
    runCommand(line);
  }
}

}  // namespace

// Called by the LocoNet library for OPC_SW_REQ from any device, our own included.
void notifySwitchRequest(uint16_t address, uint8_t output, uint8_t direction) {
  if (output) {  // the coil-on command carries the position; coil-off follows
    cancelRetry(address);  // the turnout was switched since
    showTurnout(address, direction != 0);
  }
}

// Called for OPC_SW_REP with the outputs of a decoder that reports them.
void notifySwitchOutputsReport(uint16_t address, uint8_t closedOutput, uint8_t thrownOutput) {
  if (closedOutput && !thrownOutput) {
    showTurnout(address, true);
  } else if (thrownOutput && !closedOutput) {
    showTurnout(address, false);
  }
}

// Called for OPC_INPUT_REP: an occupancy detector or a track contact, 1 to 4096.
void notifySensor(uint16_t address, uint8_t state) { showSensor(address, state != 0); }

// Called for OPC_SW_STATE from any device, our own included: a LONG_ACK with the
// position follows.
void notifySwitchState(uint16_t address, uint8_t output, uint8_t direction) {
  (void)output;
  (void)direction;
  questionAddress = address;
  questionAt = millis();
}

// Called for a LONG_ACK to OPC_SW_STATE: d1 is 0x3C, d2 the position.
void notifyLongAck(uint8_t d1, uint8_t d2) {
  (void)d1;
  handleSwitchState(d2);
}

// Called for OPC_GPON and OPC_GPOFF.
void notifyPower(uint8_t state) {
  if (state) {
    // First the refused commands, one after another, then the lights are synced; each
    // command takes kRefusalWindowMs.
    retryDue = millis();
    startSync(kSyncAfterPowerOnMs + refused.size() * (kRefusalWindowMs + 50));
  }
  trackPower = state ? TrackPower::On : TrackPower::Off;
  if (logging()) {
    Serial.println(state ? F("LN: track power on") : F("LN: track power off"));
  }
}

void setup() {
  Serial.begin(kTerminalBaud);
  settings::load();
  pinMode(kStatusLedPin, OUTPUT);
  pinMode(kStopPin, INPUT);
  pinMode(kGoPin, INPUT);

  LocoNet.init(kLocoNetTxPin);

  // Take the central's address; the keyboards send to it at 100 kHz.
  Wire.begin(keyboard::kCentralAddress);
  Wire.onReceive(onKeyboardMessage);
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000, true);  // never hang on a stuck bus
#endif

  Serial.print(F("Märklin Keyboard 6040 on LocoNet, "));
  printVersion();
  if (resetCause & _BV(WDRF)) {
    Serial.println(F("WARNING: restarted by the watchdog: the firmware hung"));
  }
  loadKeyMapIfChanged();
  scanKeyboards();
  if (settings::options().watchdog) {
    wdt_enable(WDTO_2S);
  }
  Serial.println(F("Type `help` for the commands."));
}

void loop() {
  wdt_reset();
  handleKeyboardRequests();
  handleCoilOffs();
  handleRetries();
  handleSync();
  handleLocoNet();
  flushKeyboardMessages();
  handleStopGo();
  handleTerminal();
  updateLed();
}
