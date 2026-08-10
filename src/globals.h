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
// --- 2 module RS485: CHINH tren GPIO 42, DU PHONG tren GPIO 18 ---------------------------
// Chi MOT module duoc doc tai mot thoi diem - chon tren Web UI (radio "Nguon RS485", panel
// Sensor Configuration), luu NVS key "rs485_bk". Doi nguon = RS485.end() + begin() lai tren
// chan moi ngay trong handleSave(), KHONG can reboot.
//
// GPIO 42 = MTMS, mot trong 4 chan JTAG ngoai (39/40/41/42). Mac dinh ESP32-S3 dung USB
// Serial/JTAG tich hop chu khong dung 4 chan nay, nen 42 la GPIO thuong - chi bi chiem neu co
// ai do dot eFuse JTAG_SEL_ENABLE de chuyen sang JTAG qua chan ngoai. Khong phai chan strapping
// (0, 3, 45, 46), khong dinh SPI flash noi (26-32) hay PSRAM octal (33-37).
#define RS485_RX_MAIN   42
#define RS485_RX_BACKUP 18

// -1 = khong gan chan TX. Thiet bi CHI NGHE: node cam bien tu day so do len, firmware khong
// bao gio phat gi ra bus ca. Giu -1 de UART khong chiem mot chan chang de lam gi, va de khong
// co duong nao lo phat vao bus ban song cong dung chung.
#define RS485_TX -1

// false = dang doc module CHINH (GPIO 42), true = module DU PHONG (GPIO 18).
extern bool rs485UseBackup;
uint8_t rs485ActiveRxPin();
void rs485ApplyRxPin();   // end() + begin() lai RS485 tren chan dang duoc chon

// --- 2 nut nhan tay SONG SONG (INPUT_PULLUP, nhan = keo xuong mass) ----------------------
// Dung khi cam bien hong/offline ma van phai day show chay tiep. Bam luan phien:
//   dang TU DONG   -> bam: ban cue FULL va VAO che do tay (cam bien ngung dieu khien)
//   dang CHE DO TAY -> bam: ban cue MISSING va TRA quyen lai cho cam bien
// Hai nut CHUC NANG Y HET NHAU (dat 2 vi tri khac nhau trong phong), bam nut nao cung di
// mot buoc trong cung chu ky do - khong phai nut nay FULL nut kia MISSING.
// Xem checkButtons() va manualOverride trong cantim_mqtt_new.cpp.
// GPIO 1 va 2 dang trong: ETH dung 10/11/12/13, relay 4/5/6/7, RS485 dung 42 (chinh) va 18
// (du phong). Ca hai deu khong phai chan strapping cua ESP32-S3 (0, 3, 45, 46).
#define BTN_MANUAL_COUNT 2
#define BTN_MANUAL_PIN  1
#define BTN_MANUAL_PIN2 2
#define BTN_ACTIVE LOW
#define BTN_DEBOUNCE_MS 50UL

// Khoang cach toi thieu giua HAI buoc bam tay lien tiep, CA HAI CHIEU: bam vao che do tay
// (cue FULL) roi phai cho ngan nay moi thoat (cue MISSING) duoc, va thoat xong cung phai cho
// ngan nay moi kich FULL lai duoc. Bam trong khoang nay bi BO QUA hoan toan.
//
// Ly do: gio co 2 nut dat 2 cho, mot cu bam nhan hai lan hoac hai nguoi cung bam se ban 2 cue
// nguoc nhau cach nhau vai phan giay - ben nhan cue coi nhu chua tung co cue dau. Chieu quay
// lai FULL con them mot ly do nua: cue MISSING vua ban ra thuong la de dong canh/tra phong ve
// trang thai cho, dap FULL de len ngay se cat ngang chinh cai hieu ung do.
#define MANUAL_STEP_HOLD_MS 5000UL

// millis() cua buoc bam tay duoc CHAP NHAN gan nhat (ca chieu vao lan chieu ra). 0 = chua co
// buoc nao ke tu luc boot, luc do khong khoa gi - neu khong thi 5 giay dau sau khi cap dien
// nut se cam nhu hong.
extern unsigned long manualStepAt;

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

// Bao nhiêu sensor phải RA NGOÀI range (min/max) thì mới NGẮT FULL về MISSING.
//
// 2026-08-10: ngưỡng này KHÔNG còn đối xứng - nó chỉ điều khiển chiều RA. Máy trạng thái
// chạy kiểu hysteresis (xem checkDistance()):
//
//   đang MISSING -> FULL : phải TẤT CẢ sensor (enable + online) đều trong range
//   đang FULL -> MISSING : phải có >= missingThreshold sensor ngoài range
//   ở giữa               : GIỮ NGUYÊN trạng thái đang có
//
// Vùng chết ở giữa là mục đích chính: mở cue thì đòi đủ hết, nhưng khi show đang chạy thì một
// sensor nhiễu/lệch tạm không cắt ngang cue. missingThreshold = 1 cho ra đúng hành vi cũ
// (không có vùng chết).
//
// Chỉ tính trên sensor đang enable VÀ đang online - sensor offline bị loại khỏi requiredCount
// nên không bị tính là "ngoài range". Hệ quả cần biết: mất 1 sensor thì tiêu chuẩn vào FULL
// tự hạ theo (chỉ cần các con còn sống đều trong range). Đây là lựa chọn có chủ đích - show
// vẫn chạy được khi hỏng phần cứng - và dashboard hiện rõ "n/m sensor online" để không ai
// tưởng vẫn đang đủ bộ.
//
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

// --- Ma nhan dang ban firmware dang chay (2026-08-10) ------------------------------------
// Tinh tu CHINH anh da nap (ESP.getSketchMD5()), khong phai tu macro __DATE__/__TIME__.
// Ly do: PlatformIO chi bien dich lai file nao thay doi, nen dau thoi gian compile nam trong
// web.cpp se giu nguyen gia tri CU neu lan build do chi sua html.cpp - tuc no sai dung luc can
// no nhat, la luc kiem tra xem OTA da that su doi firmware chua. MD5 doc tu flash thi khong
// bao gio noi doi: byte khac nhau la ma khac nhau.
//
// Tinh 1 lan trong setup() roi cache: getSketchMD5() phai doc het ~1.2MB flash, khong the goi
// moi lan /data (dashboard poll 4 lan/giay).
extern char fwId[9];      // 8 ky tu dau cua MD5 sketch
extern uint32_t fwSize;   // kich thuoc sketch (byte)
void fwIdInit();

// --- OTA tu URL (2026-08-10) --------------------------------------------------------------
// Thay vi chon file upload qua form, board TU TAI firmware.bin ve tu mot URL da luu san (NVS
// key "ota_url") roi tu ghi flash va reboot. Tien khi phai nap nhieu board giong nhau: bat mot
// HTTP server trong LAN, moi board chi con mot nut bam.
//
// CHI HO TRO http:// - https:// can NetworkClientSecure kem chung chi, ton them ~100KB flash va
// them may kieu loi kho doan; trong mang show khep kin thi khong dang. handleUpdateUrl() TU CHOI
// thang https:// ngay luc luu, thay vi de no that bai luc dang tai.
//
// BAO MAT: ai kiem soat duoc URL nay thi kiem soat duoc firmware cua board. Chi tro vao may
// trong mang noi bo, dung tro ra Internet qua HTTP tran.
extern char otaUrl[96];

// Dat true tu handleUpdateUrl(); otaUrlTick() trong loop() moi thuc su tai. KHONG goi
// httpUpdate.update() thang trong handler: no chan 10-30 giay roi reboot giua chung, response
// chua kip ra khoi socket nen trinh duyet bao loi mang du update chay dung.
extern bool otaUrlPending;
void otaUrlTick();

// Returns the number of failed put*() calls (0 = fully saved), or -1 if
// prefs.begin() itself failed (namespace could not be opened, nothing was written).
int saveDistanceConfig();
