#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include "relay.h"
#include "sensor_logic.h"
#include <Arduino.h>
#include <ETH.h>
#include <SPI.h>
#include <WiFi.h>
#include <cstring>

#define DEBUG_RS485 1  // 1 = in id/distance mỗi lần nhận dòng RS485 hợp lệ ra Serial, 0 = tắt

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
char oscIp[32] = "192.168.99.100";
uint16_t oscPort = 9000;
char oscAddressFull[64] = "/composition/layers/1/clips/1/connect";
char oscAddressMissing[64] = "/composition/layers/1/clips/1/connect";
int oscValueFull = 1;
int oscValueMissing = 0;

char authUser[32] = "admin";  // Web UI Basic Auth username (F6) - change via Admin Auth panel
char authPass[32] = "admin";  // Web UI Basic Auth password (F6) - change via Admin Auth panel

int rsDistance[DEVICE_NUM];         // Khoảng cách hiện tại của mỗi sensor
unsigned long lastRS485[DEVICE_NUM]; // Thời điểm nhận dữ liệu cuối từ sensor
bool sensorEnabled[DEVICE_NUM] = {true, true, true};
bool sensorOfflineAlerted[DEVICE_NUM] = {false, false, false}; // Da gui MQTT canh bao OFFLINE chua (reset khi online lai)
int distanceMin[DEVICE_NUM] = {200, 200, 200}; // Ngưỡng min của mỗi sensor
int distanceMax[DEVICE_NUM] = {800, 800, 800}; // Ngưỡng max của mỗi sensor
bool lastState = false;             // Trạng thái đầy/vắng trước đó (raw, dùng để debounce)
bool actionDone = false;            // Đã gửi hành động MQTT chưa (cho lastState hiện tại)
bool publishedState = false;        // Trạng thái đã thực sự trigger/publish lần gần nhất (khác lastState!)
unsigned long stateTimer = 0;        // Thời gian bắt đầu xác nhận trạng thái
unsigned long confirmTime = 500;     // ms đợi xác nhận thay đổi trạng thái
unsigned long confirmTimeMissing = 1000; // ms đợi xác nhận riêng cho MISSING
bool eth_connected = false;         // Trạng thái kết nối Ethernet

// F19 static-IP fallback defaults - same /24 as this firmware's other hardcoded LAN
// defaults (mqttServer 192.168.99.225, oscIp 192.168.99.100). .199 is a judgment call
// picked to avoid colliding with those two; the operator's ACTUAL network is unknown to
// this fix and may not be 192.168.99.0/24 at all - see setup() and commit message.
char ethStaticIp[16] = "192.168.99.199";
char ethStaticGateway[16] = "192.168.99.1";
char ethStaticNetmask[16] = "255.255.255.0";
bool ethUseStaticFirst = false;

const uint8_t relayPins[RELAY_PIN_COUNT] = {4, 5, 6, 7};
bool relayPinEnabled[RELAY_PIN_COUNT] = {false, false, false, false};
bool relayActiveHigh = true;
unsigned long relayPulseMs = 3000;

// Diagnostic WiFi AP - broadcasts the current ETH IP as its SSID for a few minutes after
// boot so an operator can read the device's IP off a phone's WiFi list instead of needing
// a USB cable + Serial monitor (see startDiagAp()). File-local only (static), nothing
// outside this file needs it.
static const unsigned long DIAG_AP_DURATION_MS = 5UL * 60UL * 1000UL; // 5 phut
// Mat khau cho diag AP. Dung chung "12121212" voi cac phong khac trong cum de operator chi
// phai nho mot cai. Luu y WPA2 yeu cau toi thieu 8 ky tu - ngan hon thi softAP() se fail va
// mat luon duong vao nay.
static const char *DIAG_AP_PASS = "12121212";
static bool diagApActive = false;
static unsigned long diagApStartMs = 0;

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

// Called once right after eth_connected becomes true (DHCP success or static fallback -
// both leave the real IP readable via ETH.localIP()). Protected with DIAG_AP_PASS: this AP
// used to be open on the reasoning that it only needs to be READABLE in a WiFi scan list,
// never connected to - but the WebServer listens on every netif, so anyone joining it landed
// straight on the (unauthenticated) "/" config page at 192.168.4.1. If nobody is meant to
// connect, a password costs nothing and closes that path. Auto-off
// after DIAG_AP_DURATION_MS is handled in loop() so this doesn't keep the radio on (RF
// noise + power) once the diagnostic window has passed. `isFallback` (caller knows which
// branch of setup()'s ETH block succeeded) picks the SSID prefix so an operator can tell
// at a glance whether the device is on the router's real DHCP address or stuck on the
// fixed F19 fallback (192.168.99.199 by default) - same IP text alone doesn't say which.
static void startDiagAp(bool isFallback)
{
  String ssid = (isFallback ? "CANTIM-STATIC-" : "CANTIM-DHCP-") + ETH.localIP().toString();
  if (WiFi.softAP(ssid.c_str(), DIAG_AP_PASS)) {
    diagApActive = true;
    diagApStartMs = millis();
    LOG("Diag AP: broadcasting '%s' for %lu min", ssid.c_str(), DIAG_AP_DURATION_MS / 60000UL);
  } else {
    LOG("Diag AP: WiFi.softAP() failed");
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
    confirmTimeMissing = prefs.getULong(NVS_KEY("confirm_miss"), 2000);

    strncpy(authUser, prefs.getString(NVS_KEY("auth_user"), "admin").c_str(), sizeof(authUser) - 1);
    authUser[sizeof(authUser) - 1] = '\0';
    strncpy(authPass, prefs.getString(NVS_KEY("auth_pass"), "admin").c_str(), sizeof(authPass) - 1);
    authPass[sizeof(authPass) - 1] = '\0';

    strncpy(ethStaticIp, prefs.getString(NVS_KEY("eth_ip"), "192.168.99.199").c_str(), sizeof(ethStaticIp) - 1);
    ethStaticIp[sizeof(ethStaticIp) - 1] = '\0';
    strncpy(ethStaticGateway, prefs.getString(NVS_KEY("eth_gw"), "192.168.99.1").c_str(), sizeof(ethStaticGateway) - 1);
    ethStaticGateway[sizeof(ethStaticGateway) - 1] = '\0';
    strncpy(ethStaticNetmask, prefs.getString(NVS_KEY("eth_mask"), "255.255.255.0").c_str(), sizeof(ethStaticNetmask) - 1);
    ethStaticNetmask[sizeof(ethStaticNetmask) - 1] = '\0';
    ethUseStaticFirst = prefs.getBool(NVS_KEY("eth_first"), false);

    for (int p = 0; p < RELAY_PIN_COUNT; p++) {
      relayPinEnabled[p] = prefs.getBool(("relay_p" + String(p)).c_str(), relayPinEnabled[p]);
    }
    relayActiveHigh = prefs.getBool(NVS_KEY("relay_hi"), relayActiveHigh);
    relayPulseMs = prefs.getULong(NVS_KEY("relay_ms"), relayPulseMs);

    prefs.end();
  }

  // F6: a fresh/unconfigured device is otherwise silently insecure with nobody ever told -
  // log this plainly and unconditionally at every boot while defaults are still in effect,
  // so an operator watching Serial sees it without having to go looking.
  if (strcmp(authUser, "admin") == 0 && strcmp(authPass, "admin") == 0) {
    LOG("AUTH: using default admin credentials (admin/admin) for /save, /test_iot, /test_relay, /update - "
        "change via Web UI 'Admin Auth' panel");
  }

  relayInit();

  WiFi.onEvent(WiFiEvent);
  spi.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS, ETH_INT, ETH_RST, spi)) {
    LOG("ETH Failed");
  }

  bool ethFallbackUsed = false;

  // "Ưu tiên IP tĩnh": apply the static IP right away and skip the DHCP wait entirely
  // (faster boot; useful when the network has no DHCP server, or a fixed IP is wanted for
  // certain). Falls through to the normal DHCP-then-fallback path below if the static
  // IP/gateway/netmask don't parse, so a bad entry here never leaves the device stuck.
  if (ethUseStaticFirst) {
    IPAddress ip, gw, mask;
    if (ip.fromString(ethStaticIp) && gw.fromString(ethStaticGateway) && mask.fromString(ethStaticNetmask)) {
      // Gateway as DNS1, same reasoning as the static fallback branch below (F34).
      if (ETH.config(ip, gw, mask, gw)) {
        eth_connected = true;
        ethFallbackUsed = true;
        LOG("ETH: static IP applied immediately (uu tien) - %s (gateway %s, netmask %s)", ethStaticIp, ethStaticGateway, ethStaticNetmask);
      } else {
        LOG("ETH: ETH.config() (uu tien IP tinh) failed - falling back to DHCP");
      }
    } else {
      LOG("ETH: static IP (uu tien) failed to parse - falling back to DHCP");
    }
  }

  const unsigned long ETH_WAIT_MS = 10000UL;
  unsigned long ethStart = millis();
  while (!eth_connected && (millis() - ethStart) < ETH_WAIT_MS) {
    delay(100);
  }
  if (!eth_connected) {
    LOG("ETH did not get IP within %lu ms, applying static fallback so the Web UI stays reachable (F19)", ETH_WAIT_MS);
    // F19: no DHCP server reachable and no link-local (autoip) fallback in this build's
    // sdkconfig -> without this, lwIP's DHCP client would keep retrying forever, the device
    // would never get an IP, and loop() only services the Web UI's HTTP socket while
    // eth_connected is true - the device would be permanently unreachable with no remote
    // way to reconfigure/diagnose it. ETH.config() with a non-zero local_ip stops the DHCP
    // client and applies a static IP immediately (see NetworkInterface::config()); it does
    // NOT raise ARDUINO_EVENT_ETH_GOT_IP (that event only fires from the DHCP-client IP_EVENT
    // path), so eth_connected is set here explicitly rather than relying on WiFiEvent().
    IPAddress fallbackIp, fallbackGw, fallbackMask;
    if (fallbackIp.fromString(ethStaticIp) && fallbackGw.fromString(ethStaticGateway) && fallbackMask.fromString(ethStaticNetmask)) {
      // F34: ETH.config() with only 3 args leaves DNS empty. mqttServer is fed into
      // "mqtt://%s:%u" so a hostname is a valid input, but on this static-fallback branch
      // a hostname would never resolve without a DNS server - pass the gateway as DNS1
      // (same assumption the rest of this fallback already makes: the gateway is reachable).
      if (ETH.config(fallbackIp, fallbackGw, fallbackMask, fallbackGw)) {
        eth_connected = true;
        ethFallbackUsed = true;
        LOG("ETH: static fallback applied - IP %s (gateway %s, netmask %s)", ethStaticIp, ethStaticGateway, ethStaticNetmask);
      } else {
        LOG("ETH: static fallback ETH.config() call failed - device has no IP, Web UI unreachable");
      }
    } else {
      LOG("ETH: static fallback IP/gateway/netmask string failed to parse - check eth_ip/eth_gw/eth_mask in NVS");
    }
  }

  if (eth_connected) {
    startDiagAp(ethFallbackUsed);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/test_iot", HTTP_POST, handleTestIot);
  server.on("/test_relay", HTTP_POST, handleTestRelay);
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);
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

  if (diagApActive && (millis() - diagApStartMs) >= DIAG_AP_DURATION_MS) {
    WiFi.softAPdisconnect(true);
    diagApActive = false;
    LOG("Diag AP: turned off after %lu min", DIAG_AP_DURATION_MS / 60000UL);
  }

  readRS485();
  checkDistance();
  relayTick();
}

void readRS485()
{
  static char buffer[128];
  static size_t bufPos = 0;
  static unsigned long lastByteMs = 0;

  // Cluster H / 4.4: no inter-line timeout meant a truncated/dropped line (byte loss,
  // power glitch on the bus) left partial bytes sitting in `buffer` forever, silently
  // glued onto the FRONT of whatever the next line happened to be. At 115200 baud a
  // short "id,distance\r\n" frame (a handful of bytes) transmits in well under 1ms, so
  // any real gap between bytes of the SAME line should be sub-millisecond; a few hundred
  // ms of silence mid-line can only mean the sender stopped/dropped out. 200ms is chosen
  // as a generous margin above realistic inter-byte jitter while still being short enough
  // to recover quickly once new data resumes (real bus timing on this specific
  // installation was not measured, so this is a conservative round number, not a
  // calibrated value).
  const unsigned long RS485_INTERBYTE_TIMEOUT_MS = 200;

  while (RS485.available()) {
    int c = RS485.read();
    if (c < 0) {
      break;
    }

    unsigned long now = millis();
    if (bufPos > 0 && (now - lastByteMs) > RS485_INTERBYTE_TIMEOUT_MS) {
      LOG("RS485: stale partial line dropped (inter-byte timeout)");
      bufPos = 0;
    }
    lastByteMs = now;

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
#if DEBUG_RS485
        Serial.printf("RS485 recv: id=%d distance=%d\n", id, distance);
#endif
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

    // Sensor bị timeout RS485 (offline) bị loại hẳn khỏi requiredCount, không chỉ khỏi
    // countOK - nếu vẫn tính vào requiredCount thì countOK không bao giờ theo kịp được nữa
    // (sensor offline không thể "trong ngưỡng"), khiến trạng thái kẹt ở MISSING vĩnh viễn dù
    // các sensor còn lại đang online và đúng ngưỡng. Sensor online trở lại tự động được tính
    // lại vào requiredCount ở lần loop kế tiếp.
    if (millis() - lastRS485[i] > RS485_TIMEOUT) {
      // Cạnh online -> offline: bắn cảnh báo MQTT đúng 1 lần lúc mới rớt, không lặp lại
      // mỗi vòng loop cho tới khi sensor này online trở lại (reset flag bên dưới).
      if (!sensorOfflineAlerted[i]) {
        sensorOfflineAlerted[i] = true;
        triggerSensorOffline(i + 1);
        relayTrigger(); // Kich relay reset nguon cac node ve tinh (no-op neu khong chan nao duoc chon hoac dang giua 1 xung)
      }
      continue;
    }
    sensorOfflineAlerted[i] = false;

    requiredCount++;
    int d = rsDistance[i];
    if (isDistanceInRange(d, distanceMin[i], distanceMax[i])) {
      countOK++;
    }
  }

  bool currentState = (requiredCount > 0 && countOK == requiredCount);

  // MISSING dùng confirmTimeMissing riêng (mặc định = confirmTime * 2) - cấu hình được qua
  // Web UI, không cố định x2 cứng để tránh publish MISSING nhầm khi người tạm rời sensor.
  unsigned long activeConfirmTime = currentState ? confirmTime : confirmTimeMissing;

  if (startupWaiting) {
    if (!startupStateInitialized) {
      startupState = currentState;
      startupStateInitialized = true;
      startupStateTimer = millis();
    } else if (currentState != startupState) {
      startupState = currentState;
      startupStateTimer = millis();
    } else if (millis() - startupStateTimer >= activeConfirmTime) {
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

  if (millis() - stateTimer >= activeConfirmTime) {
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
  checkStr(prefs.putString(NVS_KEY("osc_ip"), oscIp), oscIp, "osc_ip");
  checkFixed(prefs.putUShort(NVS_KEY("osc_port"), oscPort), "osc_port");
  checkStr(prefs.putString(NVS_KEY("osc_addr_full"), oscAddressFull), oscAddressFull, "osc_addr_full");
  checkStr(prefs.putString(NVS_KEY("osc_addr_miss"), oscAddressMissing), oscAddressMissing, "osc_addr_miss");
  checkFixed(prefs.putInt(NVS_KEY("osc_value_full"), oscValueFull), "osc_value_full");
  checkFixed(prefs.putInt(NVS_KEY("osc_value_miss"), oscValueMissing), "osc_value_miss");
  checkFixed(prefs.putULong(NVS_KEY("confirm"), confirmTime), "confirm");
  checkFixed(prefs.putULong(NVS_KEY("confirm_miss"), confirmTimeMissing), "confirm_miss");
  checkStr(prefs.putString(NVS_KEY("auth_user"), authUser), authUser, "auth_user");
  checkStr(prefs.putString(NVS_KEY("auth_pass"), authPass), authPass, "auth_pass");
  checkStr(prefs.putString(NVS_KEY("eth_ip"), ethStaticIp), ethStaticIp, "eth_ip");
  checkStr(prefs.putString(NVS_KEY("eth_gw"), ethStaticGateway), ethStaticGateway, "eth_gw");
  checkStr(prefs.putString(NVS_KEY("eth_mask"), ethStaticNetmask), ethStaticNetmask, "eth_mask");
  checkFixed(prefs.putBool(NVS_KEY("eth_first"), ethUseStaticFirst), "eth_first");

  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    String relayKey = "relay_p" + String(p);
    checkFixed(prefs.putBool(relayKey.c_str(), relayPinEnabled[p]), relayKey.c_str());
  }
  checkFixed(prefs.putBool(NVS_KEY("relay_hi"), relayActiveHigh), "relay_hi");
  checkFixed(prefs.putULong(NVS_KEY("relay_ms"), relayPulseMs), "relay_ms");

  checkFixed(prefs.putUInt(NVS_KEY("cfg_ver"), CFG_VERSION), "cfg_ver");

  prefs.end();

  if (failCount > 0) {
    LOG("NVS: saveDistanceConfig() had %d failed write(s)", failCount);
  }
  return failCount;
}
