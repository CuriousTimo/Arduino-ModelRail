// The settings made on the serial terminal, kept in the Nano's EEPROM: the key
// assignment of each keyboard and the options. They survive a power cycle and a new
// upload of the sketch.

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

namespace settings {

const uint16_t kLastAddress = 2048;
const uint16_t kLastSensor = 4096;

/// A key pair's entry is a LocoNet turnout address, or a sensor address with this flag:
/// the key pair then shows the sensor (occupancy detector) instead of switching.
const uint16_t kSensorFlag = 0x8000;

inline bool isSensor(uint16_t entry) { return entry & kSensorFlag; }
inline uint16_t addressPart(uint16_t entry) { return entry & ~kSensorFlag; }

struct Options {
  /// A keyboard without its own assignment switches keyboard × 16 + 1 to + 16.
  bool autoAssign;
  /// Forward the Märklin bus STOP and GO lines to LocoNet.
  bool stopGo;
  /// Do not send a key while LocoNet reports track power off.
  bool holdWithoutPower;
  /// Log keys and switch commands on the terminal.
  bool log;
  /// Restart the Nano if its loop stops for 2 s.
  bool watchdog;
  /// How long a turnout coil stays on, in 50 ms steps (IB-Switch Special Option 1);
  /// 0 switches it off when the key is released instead.
  uint8_t coilTime;
};

const uint16_t kCoilTimeStep = 50;

/// Reads the EEPROM; an empty or foreign EEPROM gets the factory settings.
void load();

/// Back to the factory settings: no keyboard has its own assignment.
void factoryReset();

Options& options();
void saveOptions();

/// Whether a keyboard has its own assignment, set on the terminal.
bool hasOwnAssignment(uint8_t keyboard);

/// The entry of a key pair (0 to 15): a turnout address 1 to 2048, a sensor address
/// 1 to 4096 with kSensorFlag, or 0 if it does nothing. The keyboard's own assignment,
/// else the automatic one (turnouts) if it is on.
uint16_t addressOf(uint8_t keyboard, uint8_t keyPair);

/// The automatic address of a key pair, whether or not the option is on.
uint16_t autoAddressOf(uint8_t keyboard, uint8_t keyPair);

/// Gives a key pair an entry (0 for none, kSensorFlag for a sensor). A keyboard without
/// its own assignment first gets the one it had, so its other keys stay as they were.
void setAddress(uint8_t keyboard, uint8_t keyPair, uint16_t address);

/// Takes away a keyboard's own assignment.
void clearAssignment(uint8_t keyboard);

}  // namespace settings

#endif
