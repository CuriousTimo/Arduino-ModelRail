/************************************************************************************************************
 *
 *  Copyright (C) 2015-2016 Timo Sariwating
 *
 *  This program is free software under GNU GPL v3 or later.
 *
 *  Description:
 *  LocoNet Packet Monitor
 *  Demonstrates usage of LocoNet.processSwitchSensorMessage()
 *  and the various notifyXXXXXXX callback functions.
 *
 *  Hardware:
 *  - Arduino with LocoNet interface
 *  - LCD connected via I2C using John Rickman's LiquidCrystal_I2C library
 *
 *  Libraries:
 *  - LocoNet: https://github.com/mrrwa/LocoNet
 *  - LiquidCrystal_I2C: https://github.com/johnrickman/LiquidCrystal_I2C
 *
 ************************************************************************************************************/

#include <LocoNet.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// Using John Rickman's LiquidCrystal_I2C library constructor
LiquidCrystal_I2C lcd(0x20, 16, 2);

lnMsg* LnPacket = nullptr;

void setup() {
  LocoNet.init();

  Serial.begin(57600);
  Serial.println(F("LocoNet Monitor"));

  lcd.init();        // Initialize the lcd
  lcd.backlight();   // Turn on backlight
  lcdPrint("Loconet", "Railcom Monitor");
}

void loop() {
  LnPacket = LocoNet.receive();
  if (LnPacket) {
    uint8_t opcode = LnPacket->sz.command;

    switch (opcode) {
      case OPC_GPON:
        lcdPrint("GLOBAL power ON", "");
        break;
      case OPC_GPOFF:
        lcdPrint("GLOBAL power OFF", "");
        break;
      default:
        break;
    }

    Serial.print(F("RX: "));
    uint8_t msgLen = getLnMsgSize(LnPacket);
    for (uint8_t i = 0; i < msgLen; ++i) {
      uint8_t val = LnPacket->data[i];
      if (val < 0x10) Serial.print('0');  // Leading zero for hex format
      Serial.print(val, HEX);
      Serial.print(' ');
    }

    if (!LocoNet.processSwitchSensorMessage(LnPacket)) {
      Serial.println();
    }
  }
}

void lcdPrint(const String& line1, const String& line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// Callback functions called by LocoNet.processSwitchSensorMessage()

void notifySensor(uint16_t address, uint8_t state) {
  Serial.print(F("Sensor: "));
  Serial.print(address);
  Serial.print(F(" - "));
  Serial.println(state ? F("Active") : F("Inactive"));
}

void notifySwitchRequest(uint16_t address, uint8_t output, uint8_t direction) {
  Serial.print(F("Switch Request: "));
  Serial.print(address);
  Serial.print(':');
  Serial.print(direction ? F("Closed") : F("Thrown"));
  Serial.print(F(" - "));
  Serial.println(output ? F("On") : F("Off"));
}

void notifySwitchReport(uint16_t address, uint8_t output, uint8_t direction) {
  Serial.print(F("Switch Report: "));
  Serial.print(address);
  Serial.print(':');
  Serial.print(direction ? F("Closed") : F("Thrown"));
  Serial.print(F(" - "));
  Serial.println(output ? F("On") : F("Off"));
}

void notifySwitchState(uint16_t address, uint8_t output, uint8_t direction) {
  Serial.print(F("Switch State: "));
  Serial.print(address);
  Serial.print(':');
  Serial.print(direction ? F("Closed") : F("Thrown"));
  Serial.print(F(" - "));
  Serial.println(output ? F("On") : F("Off"));
}

void notifyMultiSenseTransponder(uint16_t address, uint8_t zone, uint16_t locoAddress, uint8_t present) {
  Serial.print(F("Railcom Sensor "));
  Serial.print(address);
  Serial.print(F(" reports "));
  Serial.print(present ? F("present") : F("absent"));
  Serial.print(F(" of decoder address "));
  Serial.print(locoAddress);
  Serial.print(F(" in zone "));
  Serial.println(zone, HEX);

  String line1 = "Decoder " + String(locoAddress);
  String line2 = (present ? "present" : "absent") + String(" zone ") + String(address);

  lcdPrint(line1, line2);
}
