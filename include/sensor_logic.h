#pragma once

// Pure logic (no Arduino dependency) extracted from cantim_mqtt_new.cpp so it can be
// exercised by the native unit test env, which cannot link Arduino/ETH/WiFi/String.

// Parses an already-trimmed "id,distance" line (e.g. "2,455") without touching Arduino
// String. Returns false if there is no comma, or the comma is the first character.
bool parseSensorLine(const char* line, int& id, int& distance);

// Inclusive range check used to decide whether one sensor currently reads "occupied".
bool isDistanceInRange(int distance, int minDistance, int maxDistance);

// True when id is a valid 1-based device index for deviceCount devices.
bool isValidDeviceId(int id, int deviceCount);
