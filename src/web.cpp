#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include "relay.h"
#include <Arduino.h>
#include <cstring>

// F6: gate state-changing endpoints (/save, /test_mqtt, /test_osc) behind HTTP Basic Auth.
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
static bool parseValidatedLong(const String& s, long minVal, long maxVal, long& out) {
  long v = s.toInt();
  if (v < minVal || v > maxVal) {
    return false;
  }
  out = v;
  return true;
}

void handleData() {
  String data;
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

  data += "<b>STATUS:</b> ";
  if (!anySensorEnabled) {
    data += "<span style='color:orange;font-size:20px'><b>[!] NO SENSORS ENABLED</b></span>";
  } else if (lastState) {
    data += "<span style='color:green;font-size:20px'><b>✅ FULL</b></span>";
  } else {
    data += "<span style='color:red;font-size:20px'><b>❌ MISSING</b></span>";
  }
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
  bool sensorRangeInvalid = false;
  for (int i = 0; i < DEVICE_NUM; i++) {
    bool haveMin = server.hasArg("min" + String(i));
    bool haveMax = server.hasArg("max" + String(i));
    int newMin = haveMin ? server.arg("min" + String(i)).toInt() : distanceMin[i];
    int newMax = haveMax ? server.arg("max" + String(i)).toInt() : distanceMax[i];
    if (haveMin || haveMax) {
      if (newMin <= newMax) {
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

// Test buttons fire a real FULL cue on purpose (for testing MQTT/OSC wiring), but must also
// update the state machine's bookkeeping (lastState/actionDone/publishedState) to reflect that
// FULL was just (test-)published - otherwise checkDistance() keeps believing the previous
// steady-state cue is still the "already published" one and will never re-fire a real
// triggerMissing()/triggerFull() until an unrelated raw transition happens, permanently
// desyncing the receiver from the actual sensor state (new finding 4.6).
static void syncStateMachineAfterTestTrigger() {
  lastState = true;
  publishedState = true;
  actionDone = true;
  stateTimer = millis();
}

void handleTestMQTT() {
  if (!requireAuth()) {  // F6
    return;
  }
  LOG(">>> TEST MQTT <<<");
  triggerFull();
  syncStateMachineAfterTestTrigger();
  server.send(
    200,
    "text/html",
    "<script>"
    "window.location.href='/';"
    "</script>"
  );
}

void handleTestOSC() {
  if (!requireAuth()) {  // F6
    return;
  }
  LOG(">>> TEST OSC <<<");
  triggerFull();
  syncStateMachineAfterTestTrigger();
  server.send(
    200,
    "text/html",
    "<script>"
    "window.location.href='/';"
    "</script>"
  );
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
