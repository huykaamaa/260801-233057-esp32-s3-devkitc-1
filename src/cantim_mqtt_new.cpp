#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include "relay.h"
#include "sensor_logic.h"
#include <Arduino.h>
#include <ETH.h>
#include <HTTPUpdate.h>   // OTA tu URL - nam trong core Arduino-ESP32, khong phai lib ngoai
#include <SPI.h>
#include <WiFi.h>
#include <cstring>
#include "ping/ping_sock.h" // esp_ping - xac minh gateway co that su tra loi (xem gatewayReachable())

// 1 = in id/distance mỗi lần nhận dòng RS485 hợp lệ ra Serial, 0 = tắt.
// Để 0 cho bản chạy thật: 3 node gửi liên tục nên đây là dòng in KHÔNG có giới hạn tốc độ,
// vừa tốn CPU format chuỗi, vừa có thể chặn loop() tới ~100ms nếu USB đang cắm vào chỗ không
// ai đọc (HWCDC::write chờ hết tx_timeout_ms rồi mới bỏ cuộc). Bật 1 khi cần soi RS485.
#define DEBUG_RS485 0

// ======================================================================
// LOG VONG - xem globals.h
// ======================================================================
// 60 dong x 128 ky tu = 7.7KB RAM tinh. Doi lai la doc duoc dien bien khoi dong cua mot board
// nam cach xa vai chuc cay so, thu ma truoc gio chi co cap USB moi thay duoc.
//
// 60 dong du chua tron ven mot lan boot - do la doan can doc nhat. Dai hon nua thi cai can xem
// bi day ra khoi dem boi log chay dinh ky luc sau.
#define LOG_LINES 60
#define LOG_LINE_MAX 128
static char logBuf[LOG_LINES][LOG_LINE_MAX];
// uint32_t chu khong phai uint16_t: dem den 65535 roi vong ve 0 se lam logDump() tinh sai moc
// bat dau va in ra thu tu lung tung. 32 bit thi voi nhip log cua board nay la hang chuc nam.
static uint32_t logCount = 0;
// LOG() duoc goi ca tu loop() lan tu WiFiEvent() - von chay trong task su kien cua he thong -
// nen hai luong co the ghi cung luc. Spinlock chi bao doan CHEP vao dem; phan dinh dang chuoi
// (vsnprintf, cham) de ben ngoai de khong khoa lau.
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void logPrintf(const char *fmt, ...) {
  char line[LOG_LINE_MAX];

  // Dau moi dong la so giay tu luc boot. Khong co no thi mot dem log chi la mot dong chu roi
  // rac, khong biet cai nao xay ra truoc cai nao va cach nhau bao lau.
  unsigned long ms = millis();
  int n = snprintf(line, sizeof(line), "[%lu.%lus] ", ms / 1000UL, (ms % 1000UL) / 100UL);
  if (n < 0 || n >= (int)sizeof(line)) n = 0;

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);

  Serial.println(line);   // duong Serial giu nguyen, khong mat gi

  portENTER_CRITICAL(&logMux);
  strncpy(logBuf[logCount % LOG_LINES], line, LOG_LINE_MAX - 1);
  logBuf[logCount % LOG_LINES][LOG_LINE_MAX - 1] = '\0';
  logCount++;
  portEXIT_CRITICAL(&logMux);
}

String logDump() {
  String out;
  out.reserve(LOG_LINES * 64);

  portENTER_CRITICAL(&logMux);
  uint32_t total = logCount;
  portEXIT_CRITICAL(&logMux);

  // Chua vong het mot luot thi chi co `total` dong; vong roi thi dong cu nhat nam ngay sau con
  // tro ghi. Doc ngoai vung khoa: du co mot dong bi ghi de giua chung thi cung chi lam ban dung
  // dong do, khong dang de khoa ca vong lap chep chuoi.
  uint32_t start = (total > LOG_LINES) ? (total - LOG_LINES) : 0;
  for (uint32_t i = start; i < total; i++) {
    out += logBuf[i % LOG_LINES];
    out += '\n';
  }
  if (total == 0) out = "(chua co dong log nao)\n";
  return out;
}

SPIClass spi(FSPI);
WebServer server(80);                // Web server chạy trên cổng 80
// UART2 dùng cho RS485. Một UART duy nhất cho CẢ HAI module (chính GPIO 42 / dự phòng GPIO
// 18) — đổi module là gán lại chân RX, xem rs485ApplyRxPin() và globals.h.
HardwareSerial RS485(2);
Preferences prefs;                   // Lưu cấu hình không mất khi tắt
WiFiUDP oscUdp;

void readRS485();
void checkDistance();
static void checkButtons();
void updateHeartbeat();

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

char fwId[9] = "";            // Xem globals.h - 8 ky tu dau MD5 cua sketch dang chay
uint32_t fwSize = 0;

char otaUrl[96] = "";         // Xem globals.h - URL firmware.bin de board tu tai ve
bool otaUrlPending = false;   // Xem globals.h - otaUrlTick() trong loop() moi thuc su tai

bool rs485UseBackup = false;        // Xem globals.h - false = module chinh (GPIO 42)

int rsDistance[DEVICE_NUM];         // Khoảng cách hiện tại của mỗi sensor
unsigned long lastRS485[DEVICE_NUM]; // Thời điểm nhận dữ liệu cuối từ sensor
bool sensorEnabled[DEVICE_NUM] = {true, true, true};
bool sensorOfflineAlerted[DEVICE_NUM] = {false, false, false}; // Da gui MQTT canh bao OFFLINE chua (reset khi online lai)
int distanceMin[DEVICE_NUM] = {200, 200, 200}; // Ngưỡng min của mỗi sensor
int distanceMax[DEVICE_NUM] = {800, 800, 800}; // Ngưỡng max của mỗi sensor
int missingThreshold = 1;                      // Xem globals.h. 1 = hành vi cũ
bool manualOverride = false;        // Xem globals.h - true = đã bấm nút tay, logic cảm biến dừng
unsigned long manualStepAt = 0;     // Xem globals.h - millis() của bước bấm tay gần nhất
bool lastState = false;             // Trạng thái đầy/vắng trước đó (raw, dùng để debounce)
bool actionDone = false;            // Đã gửi hành động MQTT chưa (cho lastState hiện tại)
bool publishedState = false;        // Trạng thái đã thực sự trigger/publish lần gần nhất (khác lastState!)
unsigned long stateTimer = 0;        // Thời gian bắt đầu xác nhận trạng thái
// Gia tri o day PHAI trung voi default truyen cho prefs.getULong() trong setup() - truoc day
// khai bao 500/1000 con loadConfig lay 1000/2000, tuc so o dong nay la code chet va doc source
// ra sai. Gio ca 2 noi deu lay tu bien nay (xem setup()).
unsigned long confirmTime = 1000;        // ms đợi xác nhận thay đổi trạng thái
unsigned long confirmTimeMissing = 2000; // ms đợi xác nhận riêng cho MISSING

// Heartbeat/resync - port tu dat_the/gia_sach. MQTT QoS0 va OSC/UDP deu khong dam bao toi noi;
// neu dung luc doi trang thai ma mang chap chon thi ben nhan ket o cue cu CHO TOI LAN DOI
// TRANG THAI VAT LY KE TIEP - voi phong nay co the la ca luot khach. Dinh ky ban lai
// publishedState (khong doi may trang thai, chi "nhac lai" cue gan nhat). 0 = tat.
unsigned long heartbeatInterval = 60000; // ms
bool eth_connected = false;         // Trạng thái kết nối Ethernet

// F19 static-IP fallback defaults - same /24 as this firmware's other hardcoded LAN
// defaults (mqttServer 192.168.99.225, oscIp 192.168.99.100). .199 is a judgment call
// picked to avoid colliding with those two; the operator's ACTUAL network is unknown to
// this fix and may not be 192.168.99.0/24 at all - see setup() and commit message.
char ethStaticIp[16] = "192.168.99.199";
char ethStaticGateway[16] = "192.168.99.1";
char ethStaticNetmask[16] = "255.255.255.0";
bool ethUseStaticFirst = false;

// DNS DU PHONG khi chay IP TINH (2026-08-21). Nhanh IP tinh von chi dien dns1 = gateway va bo
// trong dns2 - do la mot GIA DINH: rang gateway ay co chay dich vu DNS va chiu tra loi cho
// thiet bi nay. Gia dinh do da vo o mot mang that ben phong Gia Sach: board hoi gateway roi im,
// trong khi laptop cung dat IP tinh tren dung mang do, hoi dung gateway do, lai tra ve binh
// thuong. Dien mot DNS cong cong vao o dns2 dang bo trong thi lwIP tu hoi sang no khi cai dau
// khong tra loi.
//
// Chi la duong LUI - dns1 van la gateway, mang binh thuong khong doi hanh vi gi.
static const IPAddress DNS_FALLBACK(8, 8, 8, 8);

const uint8_t relayPins[RELAY_PIN_COUNT] = {4, 5, 6, 7};
bool relayPinEnabled[RELAY_PIN_COUNT] = {false, false, false, false};
bool relayActiveHigh = true;
unsigned long relayPulseMs = 3000;

// Diagnostic WiFi AP - broadcasts the current ETH IP as its SSID so an operator can read the
// device's IP off a phone's WiFi list instead of needing a USB cable + Serial monitor (see
// startDiagAp()). File-local only (static), nothing outside this file needs it.
//
// 2026-08-21: KHONG con tu tat sau 5 phut, va gio phat CA khi khong co ETH. Ly do: nhin tu xa
// (chi bang danh sach WiFi tren dien thoai) phai phan biet duoc ba trang thai - board chay va
// co mang (thay "CANTIM-DHCP-<ip>"), board chay nhung mat mang (thay "CANTIM-NOETH"), va board
// mat dien (khong thay gi ca). Ban cu chi phat khi eth_connected va chi trong 5 phut dau, nen
// hai truong hop sau nhin y het nhau.
//
// Mat khau cho diag AP. Dung chung "12121212" voi cac phong khac trong cum de operator chi
// phai nho mot cai. Luu y WPA2 yeu cau toi thieu 8 ky tu - ngan hon thi softAP() se fail va
// mat luon duong vao nay.
static const char *DIAG_AP_PASS = "12121212";
static bool diagApActive = false;
// Ten dang phat. Giu lai de biet khi nao no khong con dung su that nua - xem diagApTick().
static String diagApSsid;
// Nhanh nao cua setup() da cap IP: true = static fallback F19. diagApTick() can no de dung
// tien to cu khi phat lai ten, nen phai o pham vi file chu khong con la bien cuc bo trong setup().
static bool ethFallbackUsed = false;

// --- Noi dung SU THAT ve trang thai mang (port tu gia_sach 8cf4cbc, 2026-08-21) ------------
// Board khong cam day mang van hien "CANTIM-STATIC-192.168.99.199" tren ten diag AP nhu the
// moi thu binh thuong, trong khi dia chi do khong ai toi duoc. Mot cai ten noi doi la ton
// nhat: nguoi ta se di tim IP do tren mang thay vi di kiem tra soi day.
//
// ethLinkPresent - co DAY dang cam va bat tay xong chua. PHAI theo doi bang su kien
// ETH_CONNECTED / ETH_DISCONNECTED chu KHONG duoc dung ETH.linkUp(): ham do thuc chat la
// NetworkInterface::linkUp(), than ham chi co "return esp_netif_is_netif_up()" - tuc no bao
// netif da duoc dung len hay chua, hoan toan khong doc trang thai PHY. Ap mot bo IP tinh cung
// lam netif up, nen hoi no "co day khong" thi cau tra loi luon la "co".
//
// ethVerified - co BANG CHUNG that su noi duoc voi mang chua: DHCP cap duoc IP (chung to co
// server tra loi) hoac gateway tra loi ping. eth_connected chi co nghia "da co mot bo IP gan
// vao netif" - dieu do van dung khi day mang nam tren ban.
static volatile bool ethLinkPresent = false;
static bool ethVerified = false;
bool ethNetVerified() { return eth_connected && ethVerified; }

void WiFiEvent(arduino_event_id_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      LOG("ETH Started");
      ETH.setHostname("ESP32-W5500");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      // Day su kien PHY that su bao "da co day va bat tay xong" - xem ethLinkPresent.
      LOG("ETH Connected");
      ethLinkPresent = true;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      LOG("ETH Got IP");
      LOG("IP: %s", ETH.localIP().toString().c_str());
      eth_connected = true;
      // DHCP cap duoc IP = co server tra loi = co mang that. Day la bang chung manh nhat,
      // khong can ping them.
      ethVerified = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      LOG("ETH Disconnected");
      eth_connected = false;
      ethLinkPresent = false;
      ethVerified = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      LOG("ETH Stopped");
      eth_connected = false;
      ethLinkPresent = false;
      ethVerified = false;
      break;
    default:
      break;
  }
}

// ======================================================================
// KIEM TRA GATEWAY CO THAT SU TRA LOI KHONG (ICMP echo)
// ======================================================================
// Ly do ton tai: ETH.config() tra ve true chi co nghia "lwIP da nhan bo IP", no KHONG thu lien
// lac voi ai ca - khong ARP, khong kiem gateway co that khong. Nen mot IP tinh dung DINH DANG
// nhung sai MANG (vd nhap 192.168.8.4 trong khi LAN la 192.168.1.x) van lam eth_connected =
// true, bo qua han vong cho DHCP va ca nhanh static fallback F19 ben duoi - board chot cung o
// mot dia chi khong ai toi duoc.
//
// Nhanh "falling back to DHCP" cu chi bat duoc truong hop KHONG PARSE DUOC, ma cai do da bi
// chan tu form Save (chi NVS hong moi lot). Tuc la no che dung truong hop khong the xay ra va
// bo trong truong hop thuc te hay gap - comment cu con khang dinh nguoc lai ("a bad entry here
// never leaves the device stuck") nen khong ai soi lai.
static volatile bool gwPingDone = false;
static volatile bool gwPingGotReply = false;

static void onGwPingSuccess(esp_ping_handle_t hdl, void *args) { gwPingGotReply = true; }
static void onGwPingEnd(esp_ping_handle_t hdl, void *args) { gwPingDone = true; }

static bool gatewayReachable(IPAddress gw)
{
  // Cho link Ethernet len truoc da: W5500 mat 1-3s de negotiate. Ping khi day chua len thi
  // chac chan khong co hoi dap va se ket luan sai la "IP tinh hong".
  const unsigned long LINK_WAIT_MS = 5000UL;
  unsigned long t0 = millis();
  while (!ETH.linkUp() && (millis() - t0) < LINK_WAIT_MS) {
    delay(50);
  }
  if (!ETH.linkUp()) {
    // Khong co day mang thi DHCP cung chet, khong ket luan duoc gi - giu nguyen IP tinh.
    LOG("ETH: chua co link sau %lu ms - bo qua buoc ping, giu IP tinh", LINK_WAIT_MS);
    return true;
  }

  ip_addr_t target;
  memset(&target, 0, sizeof(target));
  target.type = IPADDR_TYPE_V4;
  target.u_addr.ip4.addr = (uint32_t)gw; // IPAddress va ip4_addr cung network byte order

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = 3;
  cfg.interval_ms = 300;
  cfg.timeout_ms = 700;

  esp_ping_callbacks_t cbs;
  memset(&cbs, 0, sizeof(cbs));
  cbs.on_ping_success = onGwPingSuccess;
  cbs.on_ping_end = onGwPingEnd;

  gwPingDone = false;
  gwPingGotReply = false;

  esp_ping_handle_t hdl = NULL;
  if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK || hdl == NULL) {
    // Khong tao duoc phien ping la loi cua ta, khong phai loi cau hinh mang cua operator -
    // khong lay do lam co de vut bo IP tinh.
    LOG("ETH: khong tao duoc phien ping - bo qua buoc kiem tra, giu IP tinh");
    return true;
  }

  esp_ping_start(hdl);
  const unsigned long PING_TOTAL_MS = 4000UL;
  t0 = millis();
  while (!gwPingDone && (millis() - t0) < PING_TOTAL_MS) {
    delay(20);
  }
  esp_ping_stop(hdl);
  esp_ping_delete_session(hdl);

  return gwPingGotReply;
}

// Ten diag AP nen la gi NGAY LUC NAY. Ten nay la thu duy nhat doc duoc tu dien thoai khi
// khong vao duoc Web UI, nen no phai noi dung su that: mat ETH ma van khoe mot dia chi IP la
// mot loi noi doi rat dat - nguoi ta se di tim dia chi do tren mang thay vi di kiem tra soi day.
// Vi vay khong co ETH thi dan thang "NOETH" chu khong dan "0.0.0.0".
//
// Tien to STATIC/DHCP cho biet board dang o dia chi that cua router hay dang ket o IP tinh du
// phong F19 (mac dinh 192.168.99.199) - rieng chuoi IP khong noi len dieu do.
static String diagApName()
{
  // Chua co bang chung noi duoc voi mang thi KHONG dan dia chi vao ten - du netif co dang giu
  // mot bo IP tinh. Dia chi do khong ai toi duoc, dan ra chi lam nguoi doc di tim no tren mang
  // thay vi di kiem tra soi day. Gop luon hai truong hop "khong co IP" va "co IP nhung chet":
  // dung tu goc nhin nguoi van hanh, ca hai deu la khong co mang.
  if (!eth_connected || !ethVerified) return String("CANTIM-NOLINK");
  return String(ethFallbackUsed ? "CANTIM-STATIC-" : "CANTIM-DHCP-") + ETH.localIP().toString();
}

// Protected with DIAG_AP_PASS: this AP used to be open on the reasoning that it only needs to
// be READABLE in a WiFi scan list, never connected to - but the WebServer listens on every
// netif, so anyone joining it landed straight on the (unauthenticated) "/" config page at
// 192.168.4.1. If nobody is meant to connect, a password costs nothing and closes that path.
//
// KHONG con tu tat sau 5 phut - xem khai bao diagApSsid.
static void startDiagAp()
{
  String ssid = diagApName();
  if (WiFi.softAP(ssid.c_str(), DIAG_AP_PASS)) {
    diagApActive = true;
    diagApSsid = ssid;
    LOG("Diag AP: broadcasting '%s'", ssid.c_str());
  } else {
    LOG("Diag AP: WiFi.softAP() failed");
  }
}

// Ten AP phai theo kip trang thai. Truoc day 5 phut la du ngan de ETH khong kip doi gi; gio no
// phat mai nen mot cai ten sai se sai mai - va day dung la cai ten operator dua vao de ket
// luan tu xa. Rut day mang ra thi ten phai doi thanh "CANTIM-NOETH", cam lai thi phai quay ve
// dia chi that.
//
// Chi goi lai softAP() khi ten THUC SU khac: no da may dang noi vao AP ra, khong lam bua duoc.
static void diagApTick()
{
  if (!diagApActive) return;
  String want = diagApName();
  if (want == diagApSsid) return;
  LOG("Diag AP: trang thai doi '%s' -> '%s'", diagApSsid.c_str(), want.c_str());
  startDiagAp();
}

// Xem globals.h. Goi 1 lan trong setup(); log ra Serial luon de doi chieu duoc voi dashboard
// ma khong can mo trinh duyet.
void fwIdInit()
{
  String md5 = ESP.getSketchMD5();
  strncpy(fwId, md5.c_str(), sizeof(fwId) - 1);
  fwId[sizeof(fwId) - 1] = '\0';
  fwSize = ESP.getSketchSize();
  LOG("FW: id=%s size=%u bytes", fwId, (unsigned)fwSize);
}

uint8_t rs485ActiveRxPin()
{
  return rs485UseBackup ? RS485_RX_BACKUP : RS485_RX_MAIN;
}

// Mo lai UART tren chan dang duoc chon. Goi luc boot (sau khi doc NVS) va moi lan operator
// doi nguon tren Web UI - mot UART duy nhat duoc dung lai chu khong mo song song 2 cai, dung
// nghia "chon 1 trong 2 module".
//
// KHONG dung vao lastRS485[]: sau khi doi nguon, neu module moi khong co du lieu that thi cac
// sensor phai roi sang OFFLINE theo dung RS485_TIMEOUT nhu moi truong hop mat tin hieu khac.
// Lam gia lastRS485[] = millis() cho "de nhin" se khien rsDistance[] cu - so do cua module
// VUA BI BO - duoc tinh nhu du lieu song them 5 giay nua va co the lat FULL/MISSING sai.
void rs485ApplyRxPin()
{
  RS485.end();
  RS485.begin(115200, SERIAL_8N1, rs485ActiveRxPin(), RS485_TX);
  LOG("RS485: doc module %s (RX = GPIO %u)",
      rs485UseBackup ? "DU PHONG" : "CHINH", (unsigned)rs485ActiveRxPin());
}

void setup()
{
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 2000) {
    delay(10);
  }

  fwIdInit();

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
    missingThreshold = prefs.getInt(NVS_KEY("miss_thresh"), missingThreshold);
    if (missingThreshold < 1 || missingThreshold > DEVICE_NUM) {
      // NVS hong hoac du lieu tu ban firmware khac - ve mac dinh thay vi de gia tri vo nghia
      // lam hong logic FULL/MISSING am tham.
      LOG("NVS: miss_thresh = %d ngoai khoang 1..%d - dat lai ve 1", missingThreshold, DEVICE_NUM);
      missingThreshold = 1;
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

    // Truyen chinh bien lam default (giong dat_the/gia_sach) thay vi go lai so - truoc day
    // hardcode 1000/2000 o day trong khi khai bao dau file la 500/1000, hai nguon su that lech
    // nhau va cai o day am tham thang.
    confirmTime = prefs.getULong(NVS_KEY("confirm"), confirmTime);
    confirmTimeMissing = prefs.getULong(NVS_KEY("confirm_miss"), confirmTimeMissing);
    heartbeatInterval = prefs.getULong(NVS_KEY("heartbeat"), heartbeatInterval);

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

    rs485UseBackup = prefs.getBool(NVS_KEY("rs485_bk"), rs485UseBackup);

    strncpy(otaUrl, prefs.getString(NVS_KEY("ota_url"), "").c_str(), sizeof(otaUrl) - 1);
    otaUrl[sizeof(otaUrl) - 1] = '\0';

    prefs.end();
  }

  // SAU khi doc NVS: chan RX phu thuoc rs485UseBackup vua load, mo som hon thi lan nao boot
  // voi module du phong cung phai end()+begin() lai ngay - thua va de nguoi doc tuong chan
  // chinh moi la chan that su dang dung.
  rs485ApplyRxPin();

  // F6: a fresh/unconfigured device is otherwise silently insecure with nobody ever told -
  // log this plainly and unconditionally at every boot while defaults are still in effect,
  // so an operator watching Serial sees it without having to go looking.
  if (strcmp(authUser, "admin") == 0 && strcmp(authPass, "admin") == 0) {
    LOG("AUTH: using default admin credentials (admin/admin) for /save, /test_iot, /test_relay, /update, /reboot - "
        "change via Web UI 'Admin Auth' panel");
  }

  relayInit();

  // 2 nut nhan tay song song - INPUT_PULLUP, nhan = keo xuong mass (xem checkButtons()).
  pinMode(BTN_MANUAL_PIN,  INPUT_PULLUP);
  pinMode(BTN_MANUAL_PIN2, INPUT_PULLUP);

  WiFi.onEvent(WiFiEvent);
  spi.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
  if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS, ETH_INT, ETH_RST, spi)) {
    LOG("ETH Failed");
  }

  ethFallbackUsed = false;   // bien o pham vi file - xem khai bao gan dau file

  // "Ưu tiên IP tĩnh": apply the static IP right away and skip the DHCP wait entirely
  // (faster boot; useful when the network has no DHCP server, or a fixed IP is wanted for
  // certain). Ap xong PHAI ping gateway de xac minh bo IP nay thuc su noi chuyen duoc voi
  // mang - xem gatewayReachable() ve ly do parse duoc KHONG dong nghia voi dung.
  if (ethUseStaticFirst) {
    IPAddress ip, gw, mask;
    if (ip.fromString(ethStaticIp) && gw.fromString(ethStaticGateway) && mask.fromString(ethStaticNetmask)) {
      // Gateway as DNS1, same reasoning as the static fallback branch below (F34).
      if (ETH.config(ip, gw, mask, gw, DNS_FALLBACK)) {
        if (gatewayReachable(gw)) {
          eth_connected = true;
          ethFallbackUsed = true;
          LOG("ETH: static IP applied immediately (uu tien) - %s (gateway %s, netmask %s), gateway tra loi ping", ethStaticIp, ethStaticGateway, ethStaticNetmask);
        } else {
          // Gateway khong tra loi -> nhieu kha nang IP tinh sai mang. Tra netif ve DHCP
          // (local_ip = 0 lam esp_netif khoi dong lai DHCP client, xem NetworkInterface::
          // config) roi de vong cho DHCP ben duoi chay nhu binh thuong.
          //
          // Neu router chan ICMP thi day la canh bao gia: gia phai tra la ~10s cho DHCP, va
          // neu DHCP cung khong len thi nhanh static fallback F19 ben duoi VAN ap lai dung bo
          // IP tinh nay. Truong hop xau nhat chi la boot cham hon, khong mat board.
          LOG("ETH: gateway %s KHONG tra loi ping - IP tinh %s nhieu kha nang sai mang, chuyen sang thu DHCP", ethStaticGateway, ethStaticIp);
          ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
          eth_connected = false;
        }
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
      if (ETH.config(fallbackIp, fallbackGw, fallbackMask, fallbackGw, DNS_FALLBACK)) {
        eth_connected = true;
        ethFallbackUsed = true;
        // Ping o day nua chu khong chi o nhanh "uu tien IP tinh": khong hoi dap thi bo IP nay
        // chi la mot dia chi trong dep de ma khong ai toi duoc. Van GIU IP (Web UI con vao
        // duoc neu sau do co nguoi cam day) nhung KHONG bao la da vao mang - xem ethVerified.
        //
        // Bo qua ping khi khong co link: chac chan khong ai tra loi, doi them 4 giay vo ich
        // giua luc boot.
        ethVerified = ethLinkPresent && gatewayReachable(fallbackGw);
        LOG("ETH: static fallback applied - IP %s (gateway %s, netmask %s) - %s",
            ethStaticIp, ethStaticGateway, ethStaticNetmask,
            ethVerified ? "gateway tra loi ping, mang OK"
                        : (ethLinkPresent ? "gateway KHONG tra loi - CHUA VAO DUOC MANG"
                                          : "CHUA CAM DAY MANG - dia chi nay chua dung duoc"));
      } else {
        LOG("ETH: static fallback ETH.config() call failed - device has no IP, Web UI unreachable");
      }
    } else {
      LOG("ETH: static fallback IP/gateway/netmask string failed to parse - check eth_ip/eth_gw/eth_mask in NVS");
    }
  }

  // In DNS dang thuc su dung. Truoc day khong in o dau ca, nen khi "nap tu link bang ten mien"
  // im lang that bai thi khong co cach nao biet board dang hoi ai.
  if (eth_connected) {
    LOG("DNS: %s / %s", ETH.dnsIP(0).toString().c_str(), ETH.dnsIP(1).toString().c_str());
    if ((uint32_t)ETH.dnsIP(0) == 0) {
      LOG("DNS: bang DNS RONG - hostByName() se that bai NGAY, khong gui goi nao. "
          "URL dung ten mien se im lang khong chay; dung IP thi van duoc.");
    }
  }

  // Phat diag AP LUON, ke ca khi khong co ETH: ten no se la "CANTIM-NOETH", va do chinh la
  // cach phan biet tu xa giua "board chay nhung mat mang" voi "board mat dien" (khong thay AP
  // nao ca). Ban cu chi phat khi eth_connected nen hai truong hop do nhin y het nhau.
  //
  // Kem theo: khong co ETH thi AP nay la duong vao Web UI DUY NHAT (noi vao roi mo
  // http://192.168.4.1) de con sua duoc IP tinh ma khong phai vac laptop + cap Serial toi noi.
  startDiagAp();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/test_iot", HTTP_POST, handleTestIot);
  server.on("/test_relay", HTTP_POST, handleTestRelay);
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);
  server.on("/update_url", HTTP_POST, handleUpdateUrl);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.begin();
  oscUdp.begin(9000);

  mqttInit();
  LOG("HTTP Server Started");
}

void loop()
{
  // KHONG con chan theo eth_connected: WebServer lang nghe tren MOI netif, ke ca softAP. Chan o
  // day dong nghia voi "mat day mang thi mat luon Web UI qua 192.168.4.1" - dung cai duong vao
  // ma diag AP sinh ra de cung cap. handleClient() khong co client thi tra ve ngay, khong ton gi.
  server.handleClient();

  diagApTick();

  checkButtons();
  readRS485();
  checkDistance();
  updateHeartbeat();
  relayTick();
  otaUrlTick();  // CUOI loop: no chan ~10-30s roi reboot, dat truoc thi cac buoc tren bi treo theo
}

// Tai firmware tu otaUrl roi tu ghi flash + reboot. Xem globals.h ve ly do chi ho tro http://
// va ve rui ro bao mat cua viec keo firmware tu URL.
void otaUrlTick()
{
  // Cho them mot nhip sau khi handler dat co, roi moi tai. handleUpdateUrl() da goi
  // server.send() nhung byte cuoi chua chac da roi khoi socket; nhay vao update ngay se chan
  // loop ~20 giay roi reboot, trinh duyet mat ket noi va bao loi du update chay dung.
  static bool armed = false;
  static unsigned long armedAt = 0;
  const unsigned long OTA_URL_SETTLE_MS = 500UL;

  if (!otaUrlPending) {
    armed = false;
    return;
  }
  if (!armed) {
    armed = true;
    armedAt = millis();
    return;
  }
  if (millis() - armedAt < OTA_URL_SETTLE_MS) {
    return;
  }

  otaUrlPending = false;
  armed = false;

  if (otaUrl[0] == '\0') {
    LOG("OTA URL: chua luu URL nao - bo qua");
    return;
  }

  LOG(">>> OTA URL: bat dau tai %s <<<", otaUrl);

  // Tu phan giai ten host TRUOC khi giao cho httpUpdate, chi de GHI LAI dia chi ra log.
  //
  // Ly do: khi that bai, httpUpdate chi tra ve -1 "connection refused" - ma ma do dung chung
  // cho CA HAI truong hop "tra DNS hong" lan "mo TCP khong duoc" (xem NetworkClient::connect:
  // hostByName that bai thi tra 0, va HTTPClient doi 0 thanh -1). Nen mot minh ma loi do khong
  // noi duoc gi. Dong log nay tach bach hai kha nang, va quan trong hon: cho biet board dang
  // goi toi DIA CHI NAO - neu ten mien bi mang loc va tra ve mot dia chi khac thi chi o day moi
  // lo ra.
  //
  // Ket qua duoc lwIP cache nen lan tra cua httpUpdate ngay sau do khong ton them.
  {
    const char *h = otaUrl;
    if (strncmp(h, "http://", 7) == 0) h += 7;
    char host[64];
    size_t n = 0;
    while (h[n] && h[n] != ':' && h[n] != '/' && n < sizeof(host) - 1) { host[n] = h[n]; n++; }
    host[n] = '\0';

    IPAddress resolved;
    unsigned long t0 = millis();
    if (Network.hostByName(host, resolved)) {
      LOG("OTA URL: '%s' -> %s (%lu ms)", host, resolved.toString().c_str(), millis() - t0);
    } else {
      LOG("OTA URL: KHONG phan giai duoc '%s' sau %lu ms - hong o DNS, chua he mo TCP",
          host, millis() - t0);
    }
  }

  NetworkClient client;
  httpUpdate.rebootOnUpdate(true);
  // Server file tinh hay tra 301/302 (vd thieu dau / cuoi duong dan); khong bat theo redirect
  // thi bao "HTTP error 302" rat kho doan ra nguyen nhan.
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(client, otaUrl);
  switch (ret) {
    case HTTP_UPDATE_OK:
      // Thuc te khong bao gio in ra: rebootOnUpdate(true) restart ngay trong update().
      LOG("OTA URL: xong - dang reboot");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      LOG("OTA URL: server khong tra ve firmware moi");
      break;
    case HTTP_UPDATE_FAILED:
      LOG("OTA URL: THAT BAI (%d) %s", httpUpdate.getLastError(),
          httpUpdate.getLastErrorString().c_str());
      break;
  }
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

// Test buttons fire a real cue on purpose, but must also update the state machine's
// bookkeeping (lastState/actionDone/publishedState) to reflect that - otherwise checkDistance()
// keeps believing the previous steady-state cue is still the "already published" one and will
// never re-fire a real triggerMissing()/triggerFull() until an unrelated raw transition happens,
// permanently desyncing the receiver from the actual sensor state (new finding 4.6).
// true sau lan publish cue THAT dau tien (may trang thai chot xong luc boot, doi trang thai,
// hoac bam nut tay / nut Test). Heartbeat dung co nay de khong ban gi truoc do - xem
// updateHeartbeat().
static bool cuePublished = false;

void syncStateMachineAfterManualTrigger(bool state)
{
  lastState = state;
  publishedState = state;
  actionDone = true;
  stateTimer = millis();
  cuePublished = true;
}

// HAI nut nhan tay SONG SONG (BTN_MANUAL_PIN / BTN_MANUAL_PIN2), chuc nang y het nhau - dat
// 2 vi tri khac nhau trong phong cho tien tay. Bam nut nao cung di mot buoc trong CUNG mot
// chu ky luan phien duoi day (khong phai nut nay FULL, nut kia MISSING):
//
//   Dang TU DONG -> bam: ban cue FULL va VAO che do tay (cam bien ngung dieu khien)
//   Dang CHE DO TAY -> bam: ban cue MISSING va TRA quyen lai cho cam bien
//
// Luan phien bam theo manualOverride chu khong theo publishedState. Neu bam theo
// publishedState thi luc cam bien tu no dang o FULL, bam nut se ra MISSING - va khong ro la
// nen vao hay ra che do tay. Bam theo manualOverride thi nghia cua nut luon xac dinh: "di
// tiep mot buoc trong chu ky tay", va dashboard bao ro dang o nua nao.
//
// Bat SUON XUONG (nhan) chu khong doc muc - giu nut khong bao gio ban lien tuc.
//
// Hai buoc bam lien tiep phai cach nhau it nhat MANUAL_STEP_HOLD_MS (5s), ca chieu vao lan
// chieu ra - xem manualToggleStep().
//
// Vi sao buoc FULL phai CHOT chu khong chi ban 1 phat: dung tinh huong can nut nay nhat la khi
// cam bien hong/offline, luc do requiredCount = 0 nen checkDistance() tinh ra MISSING - neu
// khong chot thi cue FULL vua bam tay se bi may trang thai de nguoc lai sau confirmTimeMissing
// (~2s), tuc nut vo dung dung luc can nhat.
//
// Vi sao buoc MISSING tra ve tu dong la an toan: sau khi tra quyen, neu cam bien VAN hong thi
// no cho ra MISSING - trung dung cue vua ban nen khong co gi bi bat lai. Neu cam bien song lai
// thi no chay tiep binh thuong. Nghia la ke ca cam bien hong vinh vien, van chay duoc vo han
// chu ky: bam cho khach vao, bam lan nua khi xong, luot sau lap lai.
//
// LUU Y: nut nay KHONG dung toi sensorEnabled[] (o tick Enable tung sensor tren Web UI, co luu
// NVS). No chi lat manualOverride - hieu qua giong "tam ngung cam bien" nhung khong sua cau
// hinh da luu cua operator.
// Mot buoc trong chu ky tay. Tach rieng vi CA HAI nut deu goi vao day - hai nut la 2 vi tri
// bam khac nhau cua cung mot chuc nang, khong phai 2 chuc nang.
static void manualToggleStep()
{
  bool wantFull = !manualOverride;  // dang tu dong -> vao tay (FULL); dang tay -> ra (MISSING)

  // Khoa CA HAI CHIEU trong MANUAL_STEP_HOLD_MS ke tu buoc bam gan nhat (xem globals.h). Tru
  // unsigned nen an toan qua moc millis() tran ve 0. manualStepAt == 0 = chua bam lan nao ke
  // tu boot: khong khoa, neu khong thi 5 giay dau sau khi cap dien nut se cam nhu hong.
  //
  // Bo qua han chu khong xep hang lai: nguoi bam nhin dashboard thay dem nguoc va tu bam lai,
  // con neu de danh roi tu ban sau 5s thi cue bat ra luc khong ai cham vao nut - kho hieu hon
  // nhieu, va dung kieu cue "ma" ma ca phong khong biet tu dau ra.
  if (manualStepAt != 0 && (millis() - manualStepAt) < MANUAL_STEP_HOLD_MS) {
    LOG("NUT TAY: bo qua - buoc bam truoc moi cach %lums, phai du %lums moi bam tiep duoc",
        millis() - manualStepAt, MANUAL_STEP_HOLD_MS);
    return;
  }

  manualOverride = wantFull;
  manualStepAt = millis();

  if (wantFull) {
    LOG(">>> NUT TAY: FULL - vao CHE DO TAY, cam bien ngung dieu khien. Sau %lus bam lan nua de ban MISSING va tra ve tu dong <<<",
        MANUAL_STEP_HOLD_MS / 1000UL);
    triggerFull();
  } else {
    LOG(">>> NUT TAY: MISSING - ban cue MISSING va TRA quyen lai cho cam bien (che do tu dong). Sau %lus moi kich FULL lai duoc <<<",
        MANUAL_STEP_HOLD_MS / 1000UL);
    triggerMissing();
  }

  // Dong bo bookkeeping SAU khi da dat manualOverride: khi tra ve tu dong, buoc nay dat
  // lastState/publishedState = false nen vong checkDistance() ke tiep khong thay lech gia
  // va ban lai MISSING lan nua.
  syncStateMachineAfterManualTrigger(wantFull);
}

static void checkButtons()
{
  // Moi nut co bo debounce RIENG, khong OR chung muc doc cua 2 chan. Neu OR, mot nut bi chap
  // hoac ket o muc nhan se ghim duong tin hieu o active vinh vien va nut con lai khong bao gio
  // tao duoc suon nua - hong dung cai thu duy nhat cuu duoc show khi cam bien da chet. Tach
  // rieng thi nut ket chi ban 1 lan roi im, nut kia van bam binh thuong.
  static const uint8_t pins[BTN_MANUAL_COUNT] = { BTN_MANUAL_PIN, BTN_MANUAL_PIN2 };
  // Khoi tao o muc !BTN_ACTIVE (= "dang nhan") co chu dich: neu nut bi giu/chap ngay luc boot
  // thi khong co suon xuong nao duoc sinh ra, thiet bi khong tu ban cue luc khoi dong.
  static bool btnState[BTN_MANUAL_COUNT]   = { !BTN_ACTIVE, !BTN_ACTIVE }; // muc da debounce
  static bool btnReading[BTN_MANUAL_COUNT] = { !BTN_ACTIVE, !BTN_ACTIVE }; // muc doc gan nhat
  static unsigned long btnTimer[BTN_MANUAL_COUNT] = { 0, 0 };

  for (uint8_t i = 0; i < BTN_MANUAL_COUNT; i++) {
    bool raw = (digitalRead(pins[i]) == BTN_ACTIVE);

    if (raw != btnReading[i]) {
      btnReading[i] = raw;
      btnTimer[i] = millis();
    }

    if (millis() - btnTimer[i] >= BTN_DEBOUNCE_MS && btnState[i] != btnReading[i]) {
      btnState[i] = btnReading[i];

      if (btnState[i]) { // chi xu ly luc VUA NHAN, bo qua luc nha
        manualToggleStep();
      }
    }
  }
}

void checkDistance()
{
  // Che do tay (da bam nut FULL): dung han logic cam bien, giu nguyen cue vua bam. Heartbeat
  // van chay (nam o updateHeartbeat() trong loop()) nen cue tay van duoc nhac lai dinh ky nhu
  // binh thuong. Bam nut MISSING la thoat khoi day.
  if (manualOverride) {
    return;
  }

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

  int outOfRange = requiredCount - countOK;

  // Kẹp ngưỡng theo requiredCount: sensor offline bị loại khỏi requiredCount, nên nếu để
  // nguyên ngưỡng 3 mà chỉ còn 2 sensor online thì số ngoài range tối đa là 2, không bao giờ
  // chạm 3 -> kẹt ở FULL vĩnh viễn. Kẹp lại giữ đúng ý "phải mất HẾT mới là MISSING" thay vì
  // biến nó thành "không bao giờ MISSING".
  int effectiveThreshold = missingThreshold;
  if (effectiveThreshold > requiredCount) {
    effectiveThreshold = requiredCount;
  }

  // HYSTERESIS (2026-08-10) - ngưỡng vào và ngưỡng ra KHÁC nhau, xem globals.h:
  //   vào FULL : phải TẤT CẢ sensor enable+online trong range (outOfRange == 0)
  //   ra MISSING: phải >= effectiveThreshold sensor ngoài range
  //   ở giữa    : giữ nguyên trạng thái đang có
  //
  // Mốc so sánh là publishedState (cue ĐANG thực sự phát ra) chứ KHÔNG phải lastState:
  // lastState còn dao động trong lúc chờ confirmTime, lấy nó làm mốc thì ngưỡng tự nhảy qua
  // nhảy lại giữa "vào" và "ra" ngay trong một lần chuyển, cho ra kết quả tuỳ nhịp loop.
  //
  // requiredCount > 0 giữ ở CẢ HAI nhánh: mất hết sensor (offline sạch hoặc bỏ tick hết) thì
  // về MISSING, thiết bị không tự nhận FULL khi đang mù hoàn toàn.
  bool currentState;
  if (!publishedState) {
    currentState = (requiredCount > 0 && outOfRange == 0);
  } else {
    currentState = (requiredCount > 0 && outOfRange < effectiveThreshold);
  }

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
      cuePublished = true;

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
        cuePublished = true;
      }
      actionDone = true;
    }
  }

}

// Heartbeat: dinh ky ban LAI publishedState (cue gan nhat), khong dong gi vao lastState/
// actionDone/publishedState nen khong lam nhieu may trang thai.
//
// Truoc day nam o cuoi checkDistance() de an theo nhanh startupWaiting (return som). Gio
// checkDistance() con thoat som khi manualOverride nua, ma o che do tay VAN phai nhac lai cue
// - nen tach ra day, goi thang tu loop(), va dung co cuePublished lam dieu kien thay the:
// truoc lan publish that dau tien thi khong ban gi, tranh spam MISSING luc board vua boot
// chua doc duoc sensor nao.
void updateHeartbeat()
{
  if (heartbeatInterval == 0 || !cuePublished) {
    return;
  }

  static unsigned long lastHeartbeatMs = 0;
  static bool heartbeatArmed = false;
  if (!heartbeatArmed) {           // moc dau tien tinh tu cue that dau tien
    lastHeartbeatMs = millis();
    heartbeatArmed = true;
  } else if (millis() - lastHeartbeatMs >= heartbeatInterval) {
    lastHeartbeatMs = millis();
    LOG("Heartbeat: gui lai cue hien tai (%s)%s", publishedState ? "FULL" : "MISSING",
        manualOverride ? " [CHE DO TAY]" : "");
    if (publishedState) {
      triggerFull();
    } else {
      triggerMissing();
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
  checkFixed(prefs.putInt(NVS_KEY("miss_thresh"), missingThreshold), "miss_thresh");

  checkFixed(prefs.putBool(NVS_KEY("osc_en"), oscEnabled), "osc_en");
  checkStr(prefs.putString(NVS_KEY("osc_ip"), oscIp), oscIp, "osc_ip");
  checkFixed(prefs.putUShort(NVS_KEY("osc_port"), oscPort), "osc_port");
  checkStr(prefs.putString(NVS_KEY("osc_addr_full"), oscAddressFull), oscAddressFull, "osc_addr_full");
  checkStr(prefs.putString(NVS_KEY("osc_addr_miss"), oscAddressMissing), oscAddressMissing, "osc_addr_miss");
  checkFixed(prefs.putInt(NVS_KEY("osc_value_full"), oscValueFull), "osc_value_full");
  checkFixed(prefs.putInt(NVS_KEY("osc_value_miss"), oscValueMissing), "osc_value_miss");
  checkFixed(prefs.putULong(NVS_KEY("confirm"), confirmTime), "confirm");
  checkFixed(prefs.putULong(NVS_KEY("confirm_miss"), confirmTimeMissing), "confirm_miss");
  checkFixed(prefs.putULong(NVS_KEY("heartbeat"), heartbeatInterval), "heartbeat");
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

  checkFixed(prefs.putBool(NVS_KEY("rs485_bk"), rs485UseBackup), "rs485_bk");
  checkStr(prefs.putString(NVS_KEY("ota_url"), otaUrl), otaUrl, "ota_url");

  checkFixed(prefs.putUInt(NVS_KEY("cfg_ver"), CFG_VERSION), "cfg_ver");

  prefs.end();

  if (failCount > 0) {
    LOG("NVS: saveDistanceConfig() had %d failed write(s)", failCount);
  }
  return failCount;
}
