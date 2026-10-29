// Pins and factory settings. The key assignment and the options are set on the serial
// terminal (type `help`) and kept in EEPROM; this file only gives their starting values.

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------------------------
// Pins (Arduino Nano LocoNet/DCC Interface v3 with the Märklin I2C Expansion Board v1)
// ---------------------------------------------------------------------------------------
// LocoNet RX is fixed on D8 (the ICP1 pin the LocoNet library uses). I2C is A4 (SDA) and
// A5 (SCL); close the "i2c pullup" jumper on the interface board.
const uint8_t kLocoNetTxPin = 7;
// Through the expansion board's ribbon cable (interface board EXP2).
const uint8_t kStopPin = 12;  // Märklin bus STOP
const uint8_t kGoPin = A0;    // Märklin bus GO
const uint8_t kStatusLedPin = 13;  // the expansion board's red LED and the Nano's own

// Serial terminal speed.
const unsigned long kTerminalBaud = 115200;

// Shown at startup and by the terminal's `version` command.
const char kFirmwareVersion[] = "0.2.0";

// ---------------------------------------------------------------------------------------
// Factory settings (the terminal's `reset yes` returns to these)
// ---------------------------------------------------------------------------------------

// A keyboard without its own assignment switches 16 turnouts in a row: keyboard 1
// switches 1 to 16, keyboard 2 switches 17 to 32, keyboard n switches (n − 1) × 16 + 1 to
// n × 16 (keyboards numbered 1 to 16 as in Märklin's switch table). Terminal:
// `option auto on|off`.
const bool kFactoryAutoAssign = true;

// Forward a rising edge on the Märklin bus STOP and GO lines to LocoNet as power off
// (OPC_GPOFF) and power on (OPC_GPON). Off until the lines are measured: a floating
// line would switch the whole layout's power at random. The lines need a defined low
// level (a pull-down) and at most 5 V. Terminal: `option stopgo on|off`.
const bool kFactoryStopGo = false;

// A key press while LocoNet reports track power off is not sent: the turnout cannot
// switch, so the keyboard's lights go back to the turnout's last known position.
// Terminal: `option hold on|off`.
const bool kFactoryHoldWithoutPower = true;

// Log every key and every LocoNet switch command on the terminal.
// Terminal: `option log on|off`.
const bool kFactoryLog = true;

// How long a turnout coil stays on: the coil-off command follows the coil-on after this
// time, however long the key is held, as on an IB-Switch (its Special Option 1, also
// 200 ms). 0 to 12750 ms in steps of 50 ms; 0 switches the coil off when the key is
// released instead. Terminal: `option coil <ms>`.
const uint16_t kFactoryCoilTimeMs = 200;

// When the command station refuses a switch command (LONG_ACK: its buffer is full, or
// it is in STOP), send it again after this time, at most kRetryTries times, as an
// IB-Switch does (its Special Option 6, also 3 s).
const uint16_t kRetryAfterMs = 3000;
const uint8_t kRetryTries = 10;

// Restart the Nano if its main loop hangs for 2 s, for example on a stuck I2C bus.
// Terminal: `option watchdog on|off`.
const bool kFactoryWatchdog = true;

#endif
