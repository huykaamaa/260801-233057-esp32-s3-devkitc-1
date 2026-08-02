#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include "sensor_logic.h"
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
char mqttUser[32] = "";            // Tên đăng nhập MQTT (đặt qua Web UI, không hardcode)
char mqttPass[32] = "";            // Mật khẩu MQTT (đặt qua Web UI, không hardcode)
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
bool lastState = false;             // Trạng thái đầy/vắng trước đó (raw, dùng để debounce)
bool actionDone = false;            // Đã gửi hành động MQTT chưa (cho lastState hiện tại)
bool publishedState = false;        // Trạng thái đã thực sự trigger/publish lần gần nhất (khác lastState!)
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

  if (!prefs.begin(NVS_KEY("distance"), false)) {
    LOG("NVS: prefs.begin(distance) FAILED - using in-RAM defaults, config NOT loaded from flash");
  } else {
    uint32_t storedCfgVer = prefs.getUInt(NVS_KEY("cfg_ver"), 0);
    if (storedCfgVer < CFG_VERSION) {
      LOG("NVS: cfg_ver=%u (firmware expects %u) - saved config predates this key layout "
          "(or was never saved). Values below still load under the CURRENT key names; "
          "verify config on the Web UI, then hit Save once to re-stamp cfg_ver. To fully "
          "wipe NVS instead, run tools/full_erase.sh.",
          (unsigned)storedCfgVer, (unsigned)CFG_VERSION);
    }

    for (int i = 0; i < DEVICE_NUM; i++) {
      distanceMin[i] = prefs.getInt(("min" + String(i)).c_str(), distanceMin[i]);
      distanceMax[i] = prefs.getInt(("max" + String(i)).c_str(), distanceMax[i]);
      sensorEnabled[i] = prefs.getBool(("sensor" + String(i)).c_str(), sensorEnabled[i]);
    }
    strncpy(mqttServer, prefs.getString(NVS_KEY("mqtt_ip"), "192.168.99.225").c_str(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';

    mqttPort = prefs.getUShort(NVS_KEY("mqtt_port"), 1883);
    mqttEnabled = prefs.getBool(NVS_KEY("mqtt_en"), true);
    strncpy(mqttUser, prefs.getString(NVS_KEY("mqtt_user"), "").c_str(), sizeof(mqttUser) - 1);
    mqttUser[sizeof(mqttUser) - 1] = '\0';

    strncpy(mqttPass, prefs.getString(NVS_KEY("mqtt_pass"), "").c_str(), sizeof(mqttPass) - 1);
    mqttPass[sizeof(mqttPass) - 1] = '\0';

    strncpy(mqttTopic, prefs.getString(NVS_KEY("mqtt_topic"), "sensor/people").c_str(), sizeof(mqttTopic) - 1);
    mqttTopic[sizeof(mqttTopic) - 1] = '\0';

    strncpy(mqttFullValue, prefs.getString(NVS_KEY("mqtt_full"), "FULL").c_str(), sizeof(mqttFullValue) - 1);
    mqttFullValue[sizeof(mqttFullValue) - 1] = '\0';

    strncpy(mqttMissingValue, prefs.getString(NVS_KEY("mqtt_missing"), "MISSING").c_str(), sizeof(mqttMissingValue) - 1);
    mqttMissingValue[sizeof(mqttMissingValue) - 1] = '\0';

    oscEnabled = prefs.getBool(NVS_KEY("osc_en"), false);
    messengerEnabled = prefs.getBool(NVS_KEY("messenger_en"), false);
    strncpy(oscIp, prefs.getString(NVS_KEY("osc_ip"), "192.168.99.100").c_str(), sizeof(oscIp) - 1);
    oscIp[sizeof(oscIp) - 1] = '\0';
    oscPort = prefs.getUShort(NVS_KEY("osc_port"), 9000);
    // NOTE: NVS keys "osc_addr_full"/"osc_addr_miss" (<=15 chars) are intentionally NOT the
    // same string as the HTML/hasArg field names "osc_address_full"/"osc_address_missing"
    // (16/19 chars) - see NVS_KEY comment in globals.h (F29). Do not "fix" this back to match.
    strncpy(oscAddressFull, prefs.getString(NVS_KEY("osc_addr_full"), "/composition/layers/1/clips/1/connect").c_str(), sizeof(oscAddressFull) - 1);
    oscAddressFull[sizeof(oscAddressFull) - 1] = '\0';
    strncpy(oscAddressMissing, prefs.getString(NVS_KEY("osc_addr_miss"), "/composition/layers/1/clips/1/connect").c_str(), sizeof(oscAddressMissing) - 1);
    oscAddressMissing[sizeof(oscAddressMissing) - 1] = '\0';
    oscValueFull = prefs.getInt(NVS_KEY("osc_value_full"), 1);
    oscValueMissing = prefs.getInt(NVS_KEY("osc_value_miss"), 0);

    confirmTime = prefs.getULong(NVS_KEY("confirm"), 1000);
    prefs.end();
  }

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

      int id, distance;
      if (!parseSensorLine(data.c_str(), id, distance)) {
        LOG("Loi: khong tim thay dau phay");
        continue;
      }

      if (isValidDeviceId(id, DEVICE_NUM)) {
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
    if (isDistanceInRange(d, distanceMin[i], distanceMax[i])) {
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
      publishedState = currentState;  // ghi nhận đây là trạng thái vừa thực sự publish

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
      // Chỉ publish nếu trạng thái đã xác nhận (currentState) khác trạng thái đã publish
      // lần gần nhất - tránh trường hợp raw signal rung (jitter) làm lastState/actionDone
      // "quên" là trạng thái này đã được publish rồi, dẫn tới bắn lại đúng cue cũ nhiều lần.
      if (currentState != publishedState) {
        if (currentState) {
          LOG("FULL");
          triggerFull();
        } else {
          LOG("MISSING");
          triggerMissing();
        }
        publishedState = currentState;
      }
      actionDone = true;
    }
  }
}

int saveDistanceConfig()
{
  if (!prefs.begin(NVS_KEY("distance"), false)) {
    LOG("NVS: prefs.begin(distance) FAILED on save - nothing was written to flash");
    return -1;
  }

  int failCount = 0;

  // putX() returns bytes written; 0 means the write failed EXCEPT for putString() on a
  // legitimately empty string (mqtt_user/mqtt_pass may be blank by design), which also
  // returns 0 on success - so only flag putString() as failed when the source is non-empty.
  auto checkStr = [&](size_t written, const char* value, const char* keyForLog) {
    if (written == 0 && value[0] != '\0') {
      LOG("NVS: putString(%s) failed", keyForLog);
      failCount++;
    }
  };
  auto checkFixed = [&](size_t written, const char* keyForLog) {
    if (written == 0) {
      LOG("NVS: put(%s) failed", keyForLog);
      failCount++;
    }
  };

  checkStr(prefs.putString(NVS_KEY("mqtt_ip"), mqttServer), mqttServer, "mqtt_ip");
  checkFixed(prefs.putUShort(NVS_KEY("mqtt_port"), mqttPort), "mqtt_port");
  checkStr(prefs.putString(NVS_KEY("mqtt_user"), mqttUser), mqttUser, "mqtt_user");
  checkStr(prefs.putString(NVS_KEY("mqtt_pass"), mqttPass), mqttPass, "mqtt_pass");
  checkStr(prefs.putString(NVS_KEY("mqtt_topic"), mqttTopic), mqttTopic, "mqtt_topic");
  checkFixed(prefs.putBool(NVS_KEY("mqtt_en"), mqttEnabled), "mqtt_en");
  checkStr(prefs.putString(NVS_KEY("mqtt_full"), mqttFullValue), mqttFullValue, "mqtt_full");
  checkStr(prefs.putString(NVS_KEY("mqtt_missing"), mqttMissingValue), mqttMissingValue, "mqtt_missing");

  for (int i = 0; i < DEVICE_NUM; i++) {
    String minKey = "min" + String(i);
    String maxKey = "max" + String(i);
    String sensorKey = "sensor" + String(i);
    checkFixed(prefs.putInt(minKey.c_str(), distanceMin[i]), minKey.c_str());
    checkFixed(prefs.putInt(maxKey.c_str(), distanceMax[i]), maxKey.c_str());
    checkFixed(prefs.putBool(sensorKey.c_str(), sensorEnabled[i]), sensorKey.c_str());
  }

  checkFixed(prefs.putBool(NVS_KEY("osc_en"), oscEnabled), "osc_en");
  checkFixed(prefs.putBool(NVS_KEY("messenger_en"), messengerEnabled), "messenger_en");
  checkStr(prefs.putString(NVS_KEY("osc_ip"), oscIp), oscIp, "osc_ip");
  checkFixed(prefs.putUShort(NVS_KEY("osc_port"), oscPort), "osc_port");
  checkStr(prefs.putString(NVS_KEY("osc_addr_full"), oscAddressFull), oscAddressFull, "osc_addr_full");
  checkStr(prefs.putString(NVS_KEY("osc_addr_miss"), oscAddressMissing), oscAddressMissing, "osc_addr_miss");
  checkFixed(prefs.putInt(NVS_KEY("osc_value_full"), oscValueFull), "osc_value_full");
  checkFixed(prefs.putInt(NVS_KEY("osc_value_miss"), oscValueMissing), "osc_value_miss");
  checkFixed(prefs.putULong(NVS_KEY("confirm"), confirmTime), "confirm");
  checkFixed(prefs.putUInt(NVS_KEY("cfg_ver"), CFG_VERSION), "cfg_ver");

  prefs.end();

  if (failCount > 0) {
    LOG("NVS: saveDistanceConfig() had %d failed write(s)", failCount);
  }
  return failCount;
}
