#include "globals.h"
#include "relay.h"
#include <Arduino.h>

static bool relayPulseActive = false;
static unsigned long relayPulseStartMs = 0;

static int idleLevel() { return relayActiveHigh ? LOW : HIGH; }
static int activeLevel() { return relayActiveHigh ? HIGH : LOW; }

void relayInit() {
  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    pinMode(relayPins[p], OUTPUT);
    digitalWrite(relayPins[p], idleLevel());
  }
}

void relaySyncIdleLevel() {
  if (relayPulseActive) {
    return;
  }
  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    digitalWrite(relayPins[p], idleLevel());
  }
}

void relayTrigger() {
  bool anyEnabled = false;
  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    if (relayPinEnabled[p]) {
      anyEnabled = true;
      break;
    }
  }
  if (!anyEnabled || relayPulseActive) {
    return;
  }

  LOG("Relay: kich xung reset he thong (%lu ms)", relayPulseMs);
  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    if (relayPinEnabled[p]) {
      digitalWrite(relayPins[p], activeLevel());
    }
  }
  relayPulseActive = true;
  relayPulseStartMs = millis();
}

void relayTick() {
  if (!relayPulseActive) {
    return;
  }
  if (millis() - relayPulseStartMs >= relayPulseMs) {
    for (int p = 0; p < RELAY_PIN_COUNT; p++) {
      digitalWrite(relayPins[p], idleLevel());
    }
    relayPulseActive = false;
    LOG("Relay: da nha xung, ket thuc reset");
  }
}
