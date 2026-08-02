#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include <Arduino.h>
#include <ETH.h>
#include <SPI.h>
#include <WiFi.h>
#include <cstring>

SPIClass spi(FSPI);
WebServer server(80);                // Web server chạy trên cổng 80
HardwareSerial RS485(1);             // UART1 dùng cho RS485
Preferences prefs;                   // Lưu cấu hình không mất khi tắt
WiFiUDP oscUdp;

void readRS485();
void checkDistance();

esp_mqtt_client_handle_t mqtt = nullptr; // MQTT client handle
bool mqttConnected = false;          // Trạng thái kết nối MQTT
bool mqttEnabled = true;             // Cho phép gửi MQTT

char mqttServer[32] = "192.168.99.225"; // Địa chỉ MQTT broker
uint16_t mqttPort = 1883;            // Cổng MQTT broker
char mqttUser[32] = "huykaamaa";   // Tên đăng nhập MQTT
char mqttPass[32] = "6O6jNJip66@"; // Mật khẩu MQTT
char mqttTopic[64] = "sensor/people"; // Topic MQTT publish
char mqttFullValue[32] = "FULL";   // Payload MQTT khi đầy người
char mqttMissingValue[32] = "MISSING"; // Payload MQTT khi vắng người
bool oscEnabled = false;
bool messengerEnabled = false;
char oscIp[32] = "192.168.99.100";
uint16_t oscPort = 9000;
char oscAddress[64] = "/sensor/state";
char oscAddressFull[64] = "/composition/layers/1/clips/1/connect";
char oscAddressMissing[64] = "/composition/layers/1/clips/1/connect";
int oscValueFull = 1;
int oscValueMissing = 0;

int rsDistance[DEVICE_NUM];         // Khoảng cách hiện tại của mỗi sensor
unsigned long lastRS485[DEVICE_NUM]; // Thời điểm nhận dữ liệu cuối từ sensor
bool sensorEnabled[DEVICE_NUM] = {true, true, true};
int distanceMin[DEVICE_NUM] = {200, 200, 200}; // Ngưỡng min của mỗi sensor
int distanceMax[DEVICE_NUM] = {800, 800, 800}; // Ngưỡng max của mỗi sensor
bool lastState = false;             // Trạng thái đầy/vắng trước đó
bool actionDone = false;            // Đã gửi hành động MQTT chưa
unsigned long stateTimer = 0;        // Thời gian bắt đầu xác nhận trạng thái
unsigned long confirmTime = 500;     // ms đợi xác nhận thay đổi trạng thái
bool eth_connected = false;         // Trạng thái kết nối Ethernet

void WiFiEvent(arduino_event_id_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      LOG("ETH Started");
      ETH.setHostname("ESP32-W5500");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      LOG("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      LOG("ETH Got IP");
      LOG("IP: %s", ETH.localIP().toString().c_str());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      LOG("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      LOG("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void setup()
{
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 2000) {
    delay(10);
  }

  RS485.begin(115200, SERIAL_8N1, RS485_RX, RS485_TX);

  prefs.begin("distance", false);
  for (int i = 0; i < DEVICE_NUM; i++) {
    distanceMin[i] = prefs.getInt(("min" + String(i)).c_str(), distanceMin[i]);
    distanceMax[i] = prefs.getInt(("max" + String(i)).c_str(), distanceMax[i]);
    sensorEnabled[i] = prefs.getBool(("sensor" + String(i)).c_str(), sensorEnabled[i]);
  }
  strncpy(mqttServer, prefs.getString("mqtt_ip", "192.168.99.225").c_str(), sizeof(mqttServer) - 1);
  mqttServer[sizeof(mqttServer) - 1] = '\0';

  mqttPort = prefs.getUShort("mqtt_port", 1883);
  mqttEnabled = prefs.getBool("mqtt_en", true);
  strncpy(mqttUser, prefs.getString("mqtt_user", "").c_str(), sizeof(mqttUser) - 1);
  mqttUser[sizeof(mqttUser) - 1] = '\0';

  strncpy(mqttPass, prefs.getString("mqtt_pass", "").c_str(), sizeof(mqttPass) - 1);
  mqttPass[sizeof(mqttPass) - 1] = '\0';

  strncpy(mqttTopic, prefs.getString("mqtt_topic", "sensor/people").c_str(), sizeof(mqttTopic) - 1);
  mqttTopic[sizeof(mqttTopic) - 1] = '\0';

  strncpy(mqttFullValue, prefs.getString("mqtt_full", "FULL").c_str(), sizeof(mqttFullValue) - 1);
  mqttFullValue[sizeof(mqttFullValue) - 1] = '\0';

  strncpy(mqttMissingValue, prefs.getString("mqtt_missing", "MISSING").c_str(), sizeof(mqttMissingValue) - 1);
  mqttMissingValue[sizeof(mqttMissingValue) - 1] = '\0';

  oscEnabled = prefs.getBool("osc_en", false);
  messengerEnabled = prefs.getBool("messenger_en", false);
  strncpy(oscIp, prefs.getString("osc_ip", "192.168.99.100").c_str(), sizeof(oscIp) - 1);
  oscIp[sizeof(oscIp) - 1] = '\0';
  oscPort = prefs.getUShort("osc_port", 9000);
  strncpy(oscAddressFull, prefs.getString("osc_address_full", "/composition/layers/1/clips/1/connect").c_str(), sizeof(oscAddressFull) - 1);
  oscAddressFull[sizeof(oscAddressFull) - 1] = '\0';
  strncpy(oscAddressMissing, prefs.getString("osc_address_missing", "/composition/layers/1/clips/1/connect").c_str(), sizeof(oscAddressMissing) - 1);
  oscAddressMissing[sizeof(oscAddressMissing) - 1] = '\0';
  oscValueFull = prefs.getInt("osc_value_full", 1);
  oscValueMissing = prefs.getInt("osc_value_missing", 0);

  confirmTime = prefs.getULong("confirm", 1000);
  prefs.end();

  WiFi.onEvent(WiFiEvent);
  spi.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS, ETH_INT, ETH_RST, spi)) {
    LOG("ETH Failed");
  }

  const unsigned long ETH_WAIT_MS = 10000UL;
  unsigned long ethStart = millis();
  while (!eth_connected && (millis() - ethStart) < ETH_WAIT_MS) {
    delay(100);
  }
  if (!eth_connected) {
    LOG("ETH did not get IP within %lu ms, continuing without Ethernet", ETH_WAIT_MS);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/test_mqtt", HTTP_POST, handleTestMQTT);
  server.on("/test_osc", HTTP_POST, handleTestOSC);
  server.begin();
  oscUdp.begin(9000);

  mqttInit();
  LOG("HTTP Server Started");
}

void loop()
{
  if (eth_connected) {
    server.handleClient();
  }

  readRS485();
  checkDistance();
}

void readRS485()
{
  static char buffer[128];
  static size_t bufPos = 0;

  while (RS485.available()) {
    int c = RS485.read();
    if (c < 0) {
      break;
    }

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      buffer[bufPos] = '\0';
      bufPos = 0;

      String data(buffer);
      data.trim();
      if (data.length() == 0) {
        continue;
      }

      int comma = data.indexOf(',');
      if (comma <= 0) {
        LOG("Loi: khong tim thay dau phay");
        continue;
      }

      int id = data.substring(0, comma).toInt();
      int distance = data.substring(comma + 1).toInt();
      if (id >= 1 && id <= DEVICE_NUM) {
        rsDistance[id - 1] = distance;
        lastRS485[id - 1] = millis();
      } else {
        LOG("ID khong hop le");
      }
      continue;
    }

    if (bufPos < sizeof(buffer) - 1) {
      buffer[bufPos++] = (char)c;
    } else {
      bufPos = 0;
      LOG("RS485 buffer overflow");
    }
  }
}

void checkDistance()
{
  static bool startupWaiting = true;
  static bool startupStateInitialized = false;
  static bool startupState = false;
  static unsigned long startupStateTimer = 0;

  int countOK = 0;
  int requiredCount = 0;
  for (int i = 0; i < DEVICE_NUM; i++) {
    if (!sensorEnabled[i]) {
      continue;
    }

    requiredCount++;
    if (millis() - lastRS485[i] > RS485_TIMEOUT) {
      continue;
    }
    int d = rsDistance[i];
    if (d >= distanceMin[i] && d <= distanceMax[i]) {
      countOK++;
    }
  }

  bool currentState = (requiredCount > 0 && countOK == requiredCount);

  if (startupWaiting) {
    if (!startupStateInitialized) {
      startupState = currentState;
      startupStateInitialized = true;
      startupStateTimer = millis();
    } else if (currentState != startupState) {
      startupState = currentState;
      startupStateTimer = millis();
    } else if (millis() - startupStateTimer >= confirmTime) {
      startupWaiting = false;
      lastState = currentState;
      stateTimer = millis();
      actionDone = true;

      if (currentState) {
        LOG("FULL");
        triggerFull();
      } else {
        LOG("MISSING");
        triggerMissing();
      }
    }
    return;
  }

  if (currentState != lastState) {
    stateTimer = millis();
    lastState = currentState;
    actionDone = false;
  }

  if (millis() - stateTimer >= confirmTime) {
    if (!actionDone) {
      if (currentState) {
        LOG("FULL");
        triggerFull();
      } else {
        LOG("MISSING");
        triggerMissing();
      }
      actionDone = true;
    }
  }
}

void saveDistanceConfig()
{
  prefs.begin("distance", false);
  prefs.putString("mqtt_ip", mqttServer);
  prefs.putUShort("mqtt_port", mqttPort);
  prefs.putString("mqtt_user", mqttUser);
  prefs.putString("mqtt_pass", mqttPass);
  prefs.putString("mqtt_topic", mqttTopic);
  prefs.putBool("mqtt_en", mqttEnabled);
  prefs.putString("mqtt_full", mqttFullValue);
  prefs.putString("mqtt_missing", mqttMissingValue);

  for (int i = 0; i < DEVICE_NUM; i++) {
    prefs.putInt(("min" + String(i)).c_str(), distanceMin[i]);
    prefs.putInt(("max" + String(i)).c_str(), distanceMax[i]);
    prefs.putBool(("sensor" + String(i)).c_str(), sensorEnabled[i]);
  }

  prefs.putBool("osc_en", oscEnabled);
  prefs.putBool("messenger_en", messengerEnabled);
  prefs.putString("osc_ip", oscIp);
  prefs.putUShort("osc_port", oscPort);
  prefs.putString("osc_address_full", oscAddressFull);
  prefs.putString("osc_address_missing", oscAddressMissing);
  prefs.putInt("osc_value_full", oscValueFull);
  prefs.putInt("osc_value_missing", oscValueMissing);
  prefs.putULong("confirm", confirmTime);
  prefs.end();
}
