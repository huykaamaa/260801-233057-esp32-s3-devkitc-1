#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include "relay.h"
#include "sensor_logic.h" // isDistanceInRange() - dung o handleData de dem sensor trong nguong
#include <Arduino.h>
#include <Update.h>
#include <cstring>

// F6: gate state-changing endpoints (/save, /test_iot, /test_relay, /update) behind HTTP Basic Auth.
// Returns false (and already sent a 401) if the caller is not authenticated - callers must
// return immediately without doing any work when this returns false. Root GET "/" and
// polling GET "/data" deliberately do NOT call this (see globals.h comment on authUser).
static bool requireAuth() {
  if (!server.authenticate(authUser, authPass)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// F16: parses `s` as an integer and accepts it only if it falls within [minVal, maxVal].
// String::toInt() returns 0 for non-numeric input ("abc") and silently overflows/truncates
// when the result is later stored into a fixed-width type (e.g. "65537" -> uint16_t 1) - doing
// the range check on the full `long` result before any narrowing assignment closes both holes
// in one place instead of re-deriving it at each call site.
// Bo sung: chuoi phai la so thap phan THUAN. Rieng toInt() van cat duoi cho chuoi lai
// ("8080xyz" -> 8080) va tra ve 0 cho rac; hien tai 2 cho goi deu co minVal = 1 nen rac bi
// khoang chan loai, nhung bat ky caller moi nao cho phep 0 se am tham nhan rac thanh so 0.
static bool parseValidatedLong(const String& s, long minVal, long maxVal, long& out) {
  if (s.length() == 0 || s.length() > 10) { // >10 chu so la chac chan tran long
    return false;
  }
  for (size_t i = 0; i < s.length(); i++) {
    if (!isdigit((unsigned char)s[i])) {
      return false;
    }
  }
  long v = s.toInt();
  if (v < minVal || v > maxVal) {
    return false;
  }
  out = v;
  return true;
}

void handleData() {
  String data;
  // Trang dashboard poll route nay lien tuc khi co tab mo. Khong reserve() thi moi lan goi la
  // mot chuoi realloc tang dan, bo lai block chet giua heap.
  data.reserve(1024);

  data += "<span style='font-size:12px;color:#94a3b8'>Firmware build: " __DATE__ " " __TIME__ "</span><br>";
  data += "<b>MQTT:</b> ";
  if (!mqttEnabled) {
    data += "<span style='color:gray'>DISABLED</span>";
  } else if (mqttConnected) {
    data += "<span style='color:green'>CONNECTED</span>";
  } else {
    data += "<span style='color:red'>DISCONNECTED</span>";
  }
  data += "<br>";
  data += "<b>OSC:</b> ";
  data += oscEnabled ?
          "<span style='color:green'>ENABLED</span>"
          :
          "<span style='color:gray'>DISABLED</span>";
  data += "<br><br>";

  // F17: requiredCount==0 (every sensor disabled) hard-locks checkDistance()'s currentState at
  // MISSING - that is a real "device not configured" state, not "actually empty", and an
  // operator needs to be able to tell the two apart at a glance instead of seeing the same red
  // MISSING badge either way.
  bool anySensorEnabled = false;
  for (int i = 0; i < DEVICE_NUM; i++) {
    if (sensorEnabled[i]) {
      anySensorEnabled = true;
      break;
    }
  }

  // Che do tay phai hien that noi bat: dang o che do nay thi cam bien khong con dieu khien gi
  // ca, ma nhin vao badge FULL/MISSING thi khong the doan ra - operator se ngoi thac mac tai
  // sao dat vat len cam bien ma khong doi trang thai.
  if (manualOverride) {
    data += "<div style='background:#fff3cd;border-left:5px solid #ff9800;padding:8px 10px;"
            "border-radius:8px;margin-bottom:8px'><b style='color:#e65100'>&#9888; CHẾ ĐỘ TAY</b>"
            " - cue đang bị chốt ở FULL bằng nút nhấn, cảm biến KHÔNG điều khiển gì."
            " Bấm <b>nút MISSING</b> để bắn cue MISSING và trả quyền lại cho cảm biến.</div>";
  }

  data += "<b>STATUS:</b> ";
  if (!anySensorEnabled) {
    data += "<span style='color:orange;font-size:20px'><b>[!] NO SENSORS ENABLED</b></span>";
  } else if (lastState) {
    data += "<span style='color:green;font-size:20px'><b>✅ FULL</b></span>";
  } else {
    data += "<span style='color:red;font-size:20px'><b>❌ MISSING</b></span>";
  }
  // Voi nguong MISSING > 1 thi nhin badge FULL/MISSING khong the biet dang con cach nguong bao
  // xa - vd nguong 3, dang rot 2 cai thi van hien FULL y het luc khong rot cai nao. Dem lai o
  // day (doc lap voi checkDistance, khong dung bien chung) de thay ro.
  int okCount = 0, onlineCount = 0;
  for (int i = 0; i < DEVICE_NUM; i++) {
    if (!sensorEnabled[i] || (millis() - lastRS485[i] > RS485_TIMEOUT)) continue;
    onlineCount++;
    if (isDistanceInRange(rsDistance[i], distanceMin[i], distanceMax[i])) okCount++;
  }
  data += "<br>Trong ngưỡng: <b>" + String(okCount) + "/" + String(onlineCount) + "</b> sensor online";
  data += " &nbsp;|&nbsp; rớt <b>" + String(onlineCount - okCount) + "</b>";
  data += ", MISSING khi rớt <b>" + String(missingThreshold > onlineCount ? onlineCount : missingThreshold) + "</b>";
  data += "<br><br>";

  for (int i = 0; i < DEVICE_NUM; i++) {
    data += "Sensor ";
    data += i + 1;
    data += " ---------- ";

    if (millis() - lastRS485[i] > RS485_TIMEOUT) {
      data += "<span style='color:red'>OFFLINE</span>";
    } else {
      data += String(rsDistance[i]) + " mm";
    }
    data += "<br>";
  }

  server.send(200, "text/html", data);
}

void handleSave() {
  if (!requireAuth()) {  // F6
    return;
  }

  // F16: validate distanceMin[i] <= distanceMax[i] per sensor before accepting either half -
  // reading both first (falling back to the current live value for whichever field wasn't
  // submitted) means a bad pair is rejected as a pair instead of partially applied.
  // Ngoai phep so sanh min<=max, tung o con phai la SO THAT. Truoc day dung toInt() thang:
  // "abc" -> 0 va "-50" -> -50, ca hai deu qua duoc "min <= max" va lot vao distanceMin[i].
  // Min = 0 bien isDistanceInRange() (sensor_logic.cpp) thanh "moi khoang cach <= max deu tinh
  // la co nguoi" - sensor do dinh cung o trang thai CO va keo dieu kien FULL sai theo, trong
  // khi tren Web UI o MIN chi hien so 0 nhin nhu mot gia tri hop le. Day dung la lop loi ma
  // parseSensorLine() da chan cho duong RS485 (xem comment 4.5 trong sensor_logic.cpp: "zero
  // consumed digits ... instead of silently becoming a fabricated 0 that then masquerades as a
  // real reading") - cung loi do, duong vao tu form web thi chua chan.
  const long DISTANCE_MAX_MM = 8000; // xa hon tam moi cam bien trong cum, du de chan go nham
  bool sensorRangeInvalid = false;
  bool sensorValueInvalid = false;
  for (int i = 0; i < DEVICE_NUM; i++) {
    bool haveMin = server.hasArg("min" + String(i));
    bool haveMax = server.hasArg("max" + String(i));
    int newMin = distanceMin[i];
    int newMax = distanceMax[i];
    bool valuesOk = true;
    long parsed;

    if (haveMin) {
      if (parseValidatedLong(server.arg("min" + String(i)), 0, DISTANCE_MAX_MM, parsed)) newMin = (int)parsed;
      else valuesOk = false;
    }
    if (haveMax) {
      if (parseValidatedLong(server.arg("max" + String(i)), 0, DISTANCE_MAX_MM, parsed)) newMax = (int)parsed;
      else valuesOk = false;
    }

    if (haveMin || haveMax) {
      if (!valuesOk) {
        sensorValueInvalid = true;   // giu nguyen ca cap, khong ap dung nua o hop le
      } else if (newMin <= newMax) {
        distanceMin[i] = newMin;
        distanceMax[i] = newMax;
      } else {
        sensorRangeInvalid = true;
      }
    }
    sensorEnabled[i] = server.hasArg("sensor" + String(i));
    if (!sensorEnabled[i]) {
      // Sensor bi tat (uncheck) tren Web UI - reset flag ngay, tranh dong bang tu lan
      // offline truoc do va bo lo canh bao cho lan offline MOI sau khi bat lai (xem
      // checkDistance() trong cantim_mqtt_new.cpp - flag chi duoc dung/reset khi sensor
      // dang enable).
      sensorOfflineAlerted[i] = false;
    }
  }

  // Nguong so sensor rot moi tinh la MISSING. Reject + bao len UI thay vi clamp: day la con so
  // quyet dinh truc tiep logic FULL/MISSING cua phong, nhap sai ma bi lam tron am tham thi
  // operator tuong da doi duoc trong khi thuc te khong.
  bool missThreshInvalid = false;
  if (server.hasArg("miss_thresh")) {
    long v;
    if (parseValidatedLong(server.arg("miss_thresh"), 1, DEVICE_NUM, v)) {
      missingThreshold = (int)v;
    } else {
      missThreshInvalid = true;
    }
  }

  bool needRestartMQTT = false;

  // F33: an empty mqtt_ip would otherwise strncpy() straight into mqttServer, turning the
  // broker URI into "mqtt://:1883" - esp_mqtt_client_init() fails on that and MQTT stays
  // dead until fixed again via this same form. Blank submit = keep current, same rule as
  // mqtt_pass/auth_pass above.
  if (server.hasArg("mqtt_ip") && server.arg("mqtt_ip").length() > 0) {
    strncpy(mqttServer, server.arg("mqtt_ip").c_str(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
    needRestartMQTT = true;
  }

  // F16: reject out of range/non-numeric mqtt_port instead of silently truncating
  // ("65537" -> uint16_t 1) or accepting toInt()'s 0-for-garbage as a real port.
  bool mqttPortInvalid = false;
  if (server.hasArg("mqtt_port")) {
    long v;
    if (parseValidatedLong(server.arg("mqtt_port"), 1, 65535, v)) {
      mqttPort = (uint16_t)v;
      needRestartMQTT = true;
    } else {
      mqttPortInvalid = true;
    }
  }

  if (server.hasArg("mqtt_user")) {
    strncpy(mqttUser, server.arg("mqtt_user").c_str(), sizeof(mqttUser) - 1);
    mqttUser[sizeof(mqttUser) - 1] = '\0';
    needRestartMQTT = true;
  }

  if (server.hasArg("mqtt_enable")) {
    mqttEnabled = true;
  } else {
    mqttEnabled = false;
  }

  // F32: the form no longer echoes the saved password back into HTML (see A1 in
  // docs/todo/audit-tu-gia-sach-2026-08-03.md - reading it back via View Source/curl
  // let anyone on the LAN read the broker password off the unauthenticated "/" page),
  // so an empty submit here means "field left blank", not "clear the password" -
  // same "blank = keep current" rule already used for auth_pass above.
  if (server.hasArg("mqtt_pass") && server.arg("mqtt_pass").length() > 0) {
    strncpy(mqttPass, server.arg("mqtt_pass").c_str(), sizeof(mqttPass) - 1);
    mqttPass[sizeof(mqttPass) - 1] = '\0';
    needRestartMQTT = true;
  }

  if (server.hasArg("mqtt_topic")) {
    strncpy(mqttTopic, server.arg("mqtt_topic").c_str(), sizeof(mqttTopic) - 1);
    mqttTopic[sizeof(mqttTopic) - 1] = '\0';
  }

  if (server.hasArg("mqtt_full")) {
    strncpy(mqttFullValue, server.arg("mqtt_full").c_str(), sizeof(mqttFullValue) - 1);
    mqttFullValue[sizeof(mqttFullValue) - 1] = '\0';
  }

  if (server.hasArg("mqtt_missing")) {
    strncpy(mqttMissingValue, server.arg("mqtt_missing").c_str(), sizeof(mqttMissingValue) - 1);
    mqttMissingValue[sizeof(mqttMissingValue) - 1] = '\0';
  }

  if (server.hasArg("osc_enable")) {
    oscEnabled = true;
  } else {
    oscEnabled = false;
  }

  if (server.hasArg("osc_ip")) {
    strncpy(oscIp, server.arg("osc_ip").c_str(), sizeof(oscIp) - 1);
    oscIp[sizeof(oscIp) - 1] = '\0';
  }

  // F16: same range validation as mqtt_port above.
  bool oscPortInvalid = false;
  if (server.hasArg("osc_port")) {
    long v;
    if (parseValidatedLong(server.arg("osc_port"), 1, 65535, v)) {
      oscPort = (uint16_t)v;
    } else {
      oscPortInvalid = true;
    }
  }

  // 4.9: OSC 1.0 requires an address pattern to start with '/'. Reject (do
  // not persist) an address missing the leading slash instead of silently
  // saving/transmitting something a conformant receiver will drop - the
  // operator gets a clear signal in the Save response below.
  bool oscAddressInvalid = false;

  if (server.hasArg("osc_address_full")) {
    String v = server.arg("osc_address_full");
    if (v.length() > 0 && v[0] != '/') {
      oscAddressInvalid = true;
    } else {
      strncpy(oscAddressFull, v.c_str(), sizeof(oscAddressFull) - 1);
      oscAddressFull[sizeof(oscAddressFull) - 1] = '\0';
    }
  }

  if (server.hasArg("osc_address_missing")) {
    String v = server.arg("osc_address_missing");
    if (v.length() > 0 && v[0] != '/') {
      oscAddressInvalid = true;
    } else {
      strncpy(oscAddressMissing, v.c_str(), sizeof(oscAddressMissing) - 1);
      oscAddressMissing[sizeof(oscAddressMissing) - 1] = '\0';
    }
  }

  if (server.hasArg("osc_value_full")) {
    oscValueFull = server.arg("osc_value_full").toInt();
  }

  if (server.hasArg("osc_value_missing")) {
    oscValueMissing = server.arg("osc_value_missing").toInt();
  }

  // F15: clamp instead of reject - "-1" fed through String::toInt() into an unsigned long
  // used to wrap to 4294967295 (~49.7 days), silently disabling all FULL/MISSING triggering
  // (see checkDistance()'s startupWaiting confirm-time compare). Clamping the signed toInt()
  // result BEFORE it is ever assigned to the unsigned confirmTime prevents the wrap entirely.
  if (server.hasArg("confirm")) {
    long v = server.arg("confirm").toInt();
    if (v < 50) {
      v = 50;
    } else if (v > 60000) {
      v = 60000;
    }
    confirmTime = (unsigned long)v;
  }

  // Same clamp rule as "confirm" above - separate confirm time used only when the
  // sensor is settling into MISSING (see checkDistance() in cantim_mqtt_new.cpp).
  if (server.hasArg("confirm_miss")) {
    long v = server.arg("confirm_miss").toInt();
    if (v < 50) {
      v = 50;
    } else if (v > 60000) {
      v = 60000;
    }
    confirmTimeMissing = (unsigned long)v;
  }

  // Heartbeat: clamp thay vi reject, giong confirm/confirm_miss o tren. 0 = tat han; duoi 5s
  // la vo nghia (spam broker), tren 1 gio thi khong con la luoi an toan nua.
  if (server.hasArg("heartbeat")) {
    long v = server.arg("heartbeat").toInt();
    if (v <= 0) {
      v = 0;
    } else if (v < 5000) {
      v = 5000;
    } else if (v > 3600000) {
      v = 3600000;
    }
    heartbeatInterval = (unsigned long)v;
  }

  if (server.hasArg("auth_user") && server.arg("auth_user").length() > 0) {
    strncpy(authUser, server.arg("auth_user").c_str(), sizeof(authUser) - 1);
    authUser[sizeof(authUser) - 1] = '\0';
  }

  if (server.hasArg("auth_pass") && server.arg("auth_pass").length() > 0) {
    strncpy(authPass, server.arg("auth_pass").c_str(), sizeof(authPass) - 1);
    authPass[sizeof(authPass) - 1] = '\0';
  }

  // F19 static-IP panel: reject (do not persist) anything that doesn't parse as a
  // dotted-quad IPv4 address, same "validate before copy" rule as the other text
  // fields above - a garbage eth_ip would otherwise sit unnoticed in NVS until the
  // device happens to need the fallback path (DHCP down) and finds it broken.
  bool ethAddressInvalid = false;
  IPAddress ethParseTmp;

  if (server.hasArg("eth_ip")) {
    String v = server.arg("eth_ip");
    if (ethParseTmp.fromString(v)) {
      strncpy(ethStaticIp, v.c_str(), sizeof(ethStaticIp) - 1);
      ethStaticIp[sizeof(ethStaticIp) - 1] = '\0';
    } else {
      ethAddressInvalid = true;
    }
  }

  if (server.hasArg("eth_gw")) {
    String v = server.arg("eth_gw");
    if (ethParseTmp.fromString(v)) {
      strncpy(ethStaticGateway, v.c_str(), sizeof(ethStaticGateway) - 1);
      ethStaticGateway[sizeof(ethStaticGateway) - 1] = '\0';
    } else {
      ethAddressInvalid = true;
    }
  }

  if (server.hasArg("eth_mask")) {
    String v = server.arg("eth_mask");
    if (ethParseTmp.fromString(v)) {
      strncpy(ethStaticNetmask, v.c_str(), sizeof(ethStaticNetmask) - 1);
      ethStaticNetmask[sizeof(ethStaticNetmask) - 1] = '\0';
    } else {
      ethAddressInvalid = true;
    }
  }

  ethUseStaticFirst = server.hasArg("eth_static_first");

  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    relayPinEnabled[p] = server.hasArg("relay_p" + String(p));
  }
  relayActiveHigh = server.hasArg("relay_active_high");

  // Clamp thay vi reject, giong pattern "confirm"/"confirm_miss" o tren - xung qua ngan
  // (< 200ms) co the khong du de relay/nguon thuc su ngat-noi lai, qua dai (> 30s) khong
  // sai nhung khong co ich, chan lai de tranh nhap nham so 0 hoac so am.
  if (server.hasArg("relay_ms")) {
    long v = server.arg("relay_ms").toInt();
    if (v < 200) {
      v = 200;
    } else if (v > 30000) {
      v = 30000;
    }
    relayPulseMs = (unsigned long)v;
  }

  // Ap lai muc nghi ngay theo cau hinh MOI (pin duoc chon / active-high vua doi) - tranh
  // truong hop doi active-high/low xong 1 chan dang "nghi" theo dinh nghia cu lai thanh
  // "kich" theo dinh nghia moi ma khong co xung nao chay qua de tu sua. Khong lam gi neu
  // dang giua 1 xung that (xem relay.cpp).
  relaySyncIdleLevel();

  int saveFailCount = saveDistanceConfig();

  if (needRestartMQTT) {
    if (mqtt) {
      esp_mqtt_client_stop(mqtt);
      esp_mqtt_client_destroy(mqtt);
      mqtt = NULL;
      // 4.8: esp_mqtt_client_stop()/destroy() are app-initiated and never
      // route through mqttEvent() (MQTT_EVENT_DISCONNECTED only fires for
      // network-initiated disconnects), so mqttConnected would otherwise
      // stay stuck at its pre-destroy value and the Web UI status line
      // would keep showing "CONNECTED" even after a bad broker save.
      mqttConnected = false;
    }
    mqttInit();
  }

  // Only claim success when saveDistanceConfig() actually wrote everything - see F2:
  // this used to unconditionally say "Saved OK" even when NVS writes silently failed.
  String alertMsg;
  if (saveFailCount == 0) {
    alertMsg = "Saved OK";
  } else if (saveFailCount < 0) {
    alertMsg = "Save FAILED - NVS not accessible, check Serial log";
  } else {
    alertMsg = "Saved with " + String(saveFailCount) + " error(s) - check Serial log";
  }

  if (oscAddressInvalid) {
    alertMsg += " (OSC address rejected: must start with /)";
  }

  if (mqttPortInvalid) {
    alertMsg += " (MQTT port rejected: must be 1-65535)";
  }

  if (oscPortInvalid) {
    alertMsg += " (OSC port rejected: must be 1-65535)";
  }

  if (sensorRangeInvalid) {
    alertMsg += " (sensor min/max rejected: min must be <= max)";
  }

  if (sensorValueInvalid) {
    alertMsg += " (sensor min/max rejected: must be a whole number 0-8000 mm)";
  }

  if (missThreshInvalid) {
    alertMsg += " (nguong MISSING rejected: must be a whole number 1-" + String(DEVICE_NUM) + ")";
  }

  if (ethAddressInvalid) {
    alertMsg += " (Ethernet static IP/gateway/netmask rejected: must be a valid IPv4 address)";
  }

  server.send(
    200,
    "text/html",
    "<script>"
    "alert('" + alertMsg + "');"
    "window.location.href='/';"
    "</script>"
  );
}

// Ham dong bo bookkeeping sau khi ban cue tu ben ngoai da chuyen sang cantim_mqtt_new.cpp
// (syncStateMachineAfterManualTrigger) de dung chung voi 2 nut nhan tay - xem globals.h.
// Nut Test KHONG bat manualOverride: no chi la phep thu duong MQTT/OSC, logic cam bien phai
// tiep tuc chay binh thuong ngay sau do.

// MOT route duy nhat cho ca 2 kenh. Truoc day co /test_mqtt va /test_osc rieng nhung than ham
// khac nhau DUNG 1 DONG LOG - vi triggerFull() ban ca MQTT lan OSC: sendOscState() la dong
// cuoi cua no va nam NGOAI khoi if(mqttEnabled). Hai nut rieng gay hieu nham la test duoc
// tung kenh mot: dang soi "MQTT khong toi noi" ma bam Test MQTT roi thay ben nhan OSC phan
// hoi thi rat de ket luan nham la MQTT on.
void handleTestIot() {
  if (!requireAuth()) {  // F6
    return;
  }
  LOG(">>> TEST MQTT + OSC <<<");
  triggerFull();
  syncStateMachineAfterManualTrigger(true);
  server.send(
    200,
    "text/html",
    "<script>"
    "window.location.href='/';"
    "</script>"
  );
}

// Reset mem board. Gui response TRUOC roi moi restart (giong handleUpdateFinish) - neu goi
// ESP.restart() ngay thi trinh duyet chi thay ket noi bi cat, khong biet lenh da nhan chua.
// KHONG dung toi cac node ve tinh: chung co nguon rieng, muon reset node thi dung Test Relay.
void handleReboot() {
  if (!requireAuth()) {  // F6
    return;
  }
  LOG(">>> REBOOT <<<");
  server.send(
    200,
    "text/html",
    "<script>"
    "alert('Board dang khoi dong lai. Doi khoang 15-20 giay roi tai lai trang.');"
    "setTimeout(function(){window.location.href='/';},10000);"
    "</script>"
  );
  delay(500); // cho response gui xong truoc khi reboot
  ESP.restart();
}

void handleTestRelay() {
  if (!requireAuth()) {  // F6
    return;
  }
  LOG(">>> TEST RELAY <<<");
  relayTrigger();
  server.send(
    200,
    "text/html",
    "<script>"
    "window.location.href='/';"
    "</script>"
  );
}

// true trong suot 1 request /update tu luc auth duoc kiem tra (o buoc START) - dung chung giua
// handleUpdateUpload() (goi nhieu lan trong luc nhan tung chunk) va handleUpdateFinish() (goi 1
// lan sau khi nhan xong) vi Auth chi kiem tra duoc 1 lan luc bat dau, khong the goi lai
// requireAuth() o giua chung (header da xu ly xong).
static bool otaAuthOk = false;

void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaAuthOk = server.authenticate(authUser, authPass);
    if (!otaAuthOk) {
      LOG("OTA: tu choi upload - sai auth");
      return;
    }
    LOG("OTA: bat dau nhan file '%s'", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaAuthOk) return;
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaAuthOk) return;
    if (Update.end(true)) {
      LOG("OTA: nhan xong %u bytes", (unsigned)upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    LOG("OTA: upload bi huy giua chung");
  }
}

void handleUpdateFinish() {
  // Reset ngay khi doc: otaAuthOk la static nen no song qua nhieu request. Mot POST /update
  // KHONG kem file thi handleUpdateUpload() khong chay lan nao, va co se con giu gia tri cua
  // lan OTA truoc. Khong khai thac duoc de ghi flash (chunk START luon authenticate() lai
  // truoc khi ghi byte nao) nhung khong co ly do gi de co do song sot qua request.
  bool authed = otaAuthOk;
  otaAuthOk = false;

  if (!authed) {
    server.requestAuthentication();
    return;
  }
  bool ok = !Update.hasError();
  server.send(200, "text/html", ok
    ? "<script>alert('OTA thanh cong - dang khoi dong lai...');window.location.href='/';</script>"
    : "<script>alert('OTA THAT BAI - xem log Serial, board van chay firmware cu.');window.location.href='/';</script>");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}
