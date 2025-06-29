/************************************************************************************************************
 *
 *  Copyright (C) 2015-2016 Timo Sariwating
 *  Edited by Septillion (Timo Engelgeer) January 2, 2016
 *
 *  This program is free software under GNU GPL v3 or later.
 *
 *  Description:
 *  Loconet OPC_GPON and OPC_GPOFF Switch for Arduino Pro Mini 5V 16MHz
 *
 *  Connections:
 *  - Loconet TX pin: D7 (LOCONET_TX_PIN)
 *  - Loconet RX pin: D8 (must connect to AVR ICP pin)
 *  - Buttons: RedButtonPin (NC type, pin 3), GreenButtonPin (NO type, pin 2)
 *  - LEDs: RedLed (pin 6), GreenLed (pin 5)
 *
 *  Libraries:
 *  - Bounce2: https://github.com/thomasfredericks/Bounce2
 *  - LocoNet: https://github.com/mrrwa/LocoNet
 *
 ************************************************************************************************************/

#include <LocoNet.h>
#include <Bounce2.h>

// Pin assignments
constexpr byte RedButtonPin   = 3;  // NC button
constexpr byte GreenButtonPin = 2;  // NO button
constexpr byte RedLed         = 6;
constexpr byte GreenLed       = 5;
constexpr byte LOCONET_TX_PIN = 7;

// Globals
bool opcState = false;  // Power state: OFF = false, ON = true

Bounce buttonRed;
Bounce buttonGreen;

void setup() {
  // Initialize buttons with internal pullups
  buttonRed.attach(RedButtonPin, INPUT_PULLUP);
  buttonGreen.attach(GreenButtonPin, INPUT_PULLUP);

  // Initialize LEDs as outputs
  pinMode(RedLed, OUTPUT);
  pinMode(GreenLed, OUTPUT);

  // Initialize LocoNet interface with defined TX pin
  LocoNet.init(LOCONET_TX_PIN);
}

void sendOPC_GP(bool on) {
  lnMsg sendPacket;
  sendPacket.data[0] = on ? OPC_GPON : OPC_GPOFF;
  LocoNet.send(&sendPacket);
}

void loop() {
  updateButtons();
  processLocoNetMessages();
  updateLeds();
}

void updateButtons() {
  buttonRed.update();
  buttonGreen.update();

  if (buttonRed.rose()) {   // NC button released => OFF
    sendOPC_GP(false);
    opcState = false;
  } else if (buttonGreen.fell()) {  // NO button pressed => ON
    sendOPC_GP(true);
    opcState = true;
  }
}

void processLocoNetMessages() {
  lnMsg* packet = LocoNet.receive();
  if (packet != nullptr) {
    switch (packet->sz.command) {
      case OPC_GPON:
        opcState = true;
        break;
      case OPC_GPOFF:
        opcState = false;
        break;
      default:
        break;  // Ignore other commands
    }
  }
}

void updateLeds() {
  digitalWrite(RedLed, !opcState);
  digitalWrite(GreenLed, opcState);
}
