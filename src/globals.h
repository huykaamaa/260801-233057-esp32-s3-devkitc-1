#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WebServer.h>
#include <mqtt_client.h>
#include <WiFiUdp.h>
#include <stdint.h>

#define LOG(fmt, ...) do { Serial.printf(fmt "\r\n", ##__VA_ARGS__); } while(0)

extern SPIClass spi;
extern WebServer server;
extern HardwareSerial RS485;
extern Preferences prefs;
extern WiFiUDP oscUdp;

#define ETH_CS   10
#define ETH_MOSI 11
#define ETH_MISO 13
#define ETH_SCK  12
#define ETH_INT  -1
#define ETH_RST  -1
#define RS485_RX RX
#define RS485_TX -1

extern esp_mqtt_client_handle_t mqtt;
extern bool mqttConnected;
extern bool mqttEnabled;

extern char mqttServer[32];
extern uint16_t mqttPort;
extern char mqttUser[32];
extern char mqttPass[32];
extern char mqttTopic[64];
extern char mqttFullValue[32];
extern char mqttMissingValue[32];
extern bool oscEnabled;
extern bool messengerEnabled;
extern char oscIp[32];
extern uint16_t oscPort;
extern char oscAddressFull[64];
extern char oscAddressMissing[64];
extern int oscValueFull;
extern int oscValueMissing;

#define DEVICE_NUM 3
#define RS485_TIMEOUT 5000

extern int rsDistance[DEVICE_NUM];
extern unsigned long lastRS485[DEVICE_NUM];
extern bool sensorEnabled[DEVICE_NUM];
extern bool lastState;
extern bool actionDone;
extern unsigned long stateTimer;
extern int distanceMin[DEVICE_NUM];
extern int distanceMax[DEVICE_NUM];
extern unsigned long confirmTime;
extern bool eth_connected;

void saveDistanceConfig();
