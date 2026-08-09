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

// --- NVS / Preferences key-length guard (F1, F29) ---------------------------------------
// ESP32 NVS caps key names at NVS_KEY_NAME_MAX_SIZE = 16 bytes INCLUDING the NUL
// terminator => 15 usable chars. Preferences::putX()/getX() do NOT surface this as a
// compile or runtime error: putX() silently returns 0 (nvs_set_str/nvs_set_iX returns
// ESP_ERR_NVS_KEY_TOO_LONG under the hood) and getX() silently returns the caller's
// default. A key one character too long looks identical to "field was never saved" —
// this is exactly what caused Bug 1 (OSC Full Address surviving reboot, Full Value not).
//
// Wrap every string LITERAL used as an NVS/Preferences key (or namespace) with
// NVS_KEY(...) so an over-length key fails the BUILD instead of silently losing data
// in the field. Keys built at runtime (e.g. "min" + String(i)) can't use this — keep
// those short and bounded by construction (DEVICE_NUM is small, single-digit suffix).
//
// NVS keys are DELIBERATELY a separate namespace of names from the HTTP form field
// names in html.cpp/web.cpp's server.hasArg(...) — HTTP arg names have no length limit,
// NVS keys do. Renaming an HTTP field "for clarity" must never be copy-pasted into the
// NVS key, and vice versa (root cause of F29).
template <size_t N>
constexpr const char* NVS_KEY(const char (&s)[N]) {
  static_assert(N <= 16, "NVS key too long: max 15 chars + NUL (Preferences/NVS limit, see globals.h)");
  return s;
}

// Bump whenever the NVS key layout (names/types) for the "distance" namespace changes.
// See saveDistanceConfig()/setup() in cantim_mqtt_new.cpp for the boot-time check —
// it only LOGS a warning, it never auto-wipes (device is unattended in the field;
// silently discarding an operator's saved broker/OSC config would be worse than doing
// nothing). To fully wipe NVS on purpose, use tools/full_erase.sh (not web-exposed).
#define CFG_VERSION 1

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

// --- 1 nut nhan tay (INPUT_PULLUP, nhan = keo xuong mass) --------------------------------
// Dung khi cam bien hong/offline ma van phai day show chay tiep. Bam luan phien:
//   dang TU DONG   -> bam: ban cue FULL va VAO che do tay (cam bien ngung dieu khien)
//   dang CHE DO TAY -> bam: ban cue MISSING va TRA quyen lai cho cam bien
// Xem checkButtons() va manualOverride trong cantim_mqtt_new.cpp.
// GPIO 1 dang trong: ETH dung 10/11/12/13, relay 4/5/6/7, RS485_RX la RX (GPIO 44). Cung khong
// phai chan strapping cua ESP32-S3 (0, 3, 45, 46).
#define BTN_MANUAL_PIN 1
#define BTN_ACTIVE LOW
#define BTN_DEBOUNCE_MS 50UL

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
extern char oscIp[32];
extern uint16_t oscPort;
extern char oscAddressFull[64];
extern char oscAddressMissing[64];
extern int oscValueFull;
extern int oscValueMissing;

// --- HTTP Basic Auth for state-changing endpoints (F6) -----------------------------------
// /save, /test_iot, /test_relay, /update would otherwise accept unauthenticated POSTs from
// anyone on the LAN (or any
// page an operator has open in another tab). Gated with WebServer::authenticate()/
// requestAuthentication(). Root GET "/" and polling GET "/data" stay open on purpose - they
// only ever read/render state, and gating them would break the auto-refreshing dashboard.
// Default credentials are logged plainly at boot (see setup()) so a fresh device is never
// silently locked out or silently left on an unannounced default; change them via the Web UI
// "Admin Auth" panel same as any other saved field.
extern char authUser[32];
extern char authPass[32];

#define DEVICE_NUM 3
#define RS485_TIMEOUT 5000

extern int rsDistance[DEVICE_NUM];
extern unsigned long lastRS485[DEVICE_NUM];
extern bool sensorEnabled[DEVICE_NUM];
extern bool sensorOfflineAlerted[DEVICE_NUM]; // Da gui MQTT canh bao OFFLINE cho sensor nay trong lan mat tin hieu hien tai chua
extern bool lastState;
extern bool actionDone;
extern bool publishedState;          // Trạng thái đã thực sự publish (MQTT/OSC) lần gần nhất
extern unsigned long stateTimer;

// true = đang ở CHẾ ĐỘ TAY. checkDistance() thoát ngay, logic cảm biến dừng hẳn, cue giữ
// nguyên ở FULL. Bấm nút lần nữa để bắn cue MISSING và về lại tự động.
// Cố ý KHÔNG lưu vào NVS: mất điện / reboot là trở lại tự động, không để lại bẫy cho ca sau.
extern bool manualOverride;

// Đồng bộ lại bookkeeping của máy trạng thái sau khi một cue được bắn từ bên ngoài (nút Test
// trên Web UI, hoặc 2 nút nhấn tay). Không có bước này thì checkDistance() vẫn tin cue cũ là
// cue đã publish và sẽ không bắn lại khi trạng thái thật đổi về đúng giá trị đó.
void syncStateMachineAfterManualTrigger(bool state);
extern int distanceMin[DEVICE_NUM];
extern int distanceMax[DEVICE_NUM];

// Bao nhiêu sensor phải RỚT (ra ngoài ngưỡng min/max) thì mới chuyển sang MISSING.
// 1 = hành vi cũ (rớt 1 cái là MISSING ngay). 2 = rớt 1 cái vẫn còn FULL, rớt 2 mới MISSING.
// Chỉ tính trên các sensor đang enable VÀ đang online - sensor offline bị loại khỏi
// requiredCount nên không bị tính là "rớt" (xem checkDistance()).
// Cấu hình qua Web UI, hợp lệ 1..DEVICE_NUM.
extern int missingThreshold;
extern unsigned long confirmTime;
extern unsigned long confirmTimeMissing;   // Thời gian xác nhận riêng cho MISSING (mặc định = confirmTime * 2)
// Heartbeat/resync - định kỳ bắn LẠI cue gần nhất (FULL/MISSING) qua MQTT/OSC. Không đổi
// topic/địa chỉ, chỉ "nhắc lại" giá trị hiện tại - bù cho trường hợp một lần đổi trạng thái
// bị rớt mạng (MQTT QoS0 và OSC/UDP đều không đảm bảo gửi tới nơi). 0 = tắt.
extern unsigned long heartbeatInterval;
extern bool eth_connected;

// --- Ethernet static-IP fallback (F19) ---------------------------------------------------
// If no DHCP server is reachable, lwIP's DHCP client retries forever and this build has no
// CONFIG_LWIP_AUTOIP (no link-local self-assigned-IP fallback either) - the device would
// otherwise NEVER get an IP, and loop() only services the Web UI's HTTP socket while
// eth_connected is true, so the device becomes permanently unreachable with no way to
// reconfigure/diagnose it remotely. setup() falls back to ETH.config(ethStaticIp, ...) once
// the bounded DHCP wait (ETH_WAIT_MS) times out. NVS-backed (get in setup(), put in
// saveDistanceConfig()) like every other field here, even though there is no Web UI panel to
// edit them yet - that keeps the storage layer ready for one without another NVS migration.
extern char ethStaticIp[16];
extern char ethStaticGateway[16];
extern char ethStaticNetmask[16];

// When true, setup() applies the static IP immediately at boot and skips the DHCP wait
// entirely (faster boot; useful on networks with no DHCP server, or when a fixed IP is
// required). Falls back to the normal DHCP-then-static-fallback path below if the static
// IP/gateway/netmask don't parse, so a bad entry here never gets the device stuck.
extern bool ethUseStaticFirst;

// --- Relay reset khi sensor OFFLINE (2026-08-03) -----------------------------------------
// Kich 1 xung GPIO de dieu khien relay cat/noi nguon dien cua cac node ve tinh RS485, thay
// the thao tac "mo tu, cat nguon tay" khi 1 sensor rot mang qua lau. 4 chan GPIO CO DINH
// (khong doi duoc qua Web UI, chi tick chon dung/khong dung chan nao) - ho tro tick nhieu
// chan cung luc de gop dong nguon cho relay can dong lon hon 1 GPIO cap duoc.
#define RELAY_PIN_COUNT 4
extern const uint8_t relayPins[RELAY_PIN_COUNT];  // GPIO 4,5,6,7 co dinh
extern bool relayPinEnabled[RELAY_PIN_COUNT];     // Chan nao duoc tick chon tren Web UI
extern bool relayActiveHigh;                       // true = kich muc HIGH, false = kich muc LOW
extern unsigned long relayPulseMs;                 // Giu muc kich bao lau (ms) truoc khi tu nha ve muc nghi

// Returns the number of failed put*() calls (0 = fully saved), or -1 if
// prefs.begin() itself failed (namespace could not be opened, nothing was written).
int saveDistanceConfig();
