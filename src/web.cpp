#include "globals.h"
#include "mqtt.h"
#include "web.h"
#include "html.h"
#include <Arduino.h>
#include <cstring>

void handleData() {
  String data;
  data += "<b>MQTT:</b> ";
  data += mqttConnected ?
          "<span style='color:green'>CONNECTED</span>"
          :
          "<span style='color:red'>DISCONNECTED</span>";
  data += "<br>";
  data += "<b>OSC:</b> ";
  data += oscEnabled ?
          "<span style='color:green'>ENABLED</span>"
          :
          "<span style='color:gray'>DISABLED</span>";
  data += "<br><br>";

  data += "<b>STATUS:</b> ";
  if (lastState) {
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
  for (int i = 0; i < DEVICE_NUM; i++) {
    if (server.hasArg("min" + String(i))) {
      distanceMin[i] = server.arg("min" + String(i)).toInt();
    }
    if (server.hasArg("max" + String(i))) {
      distanceMax[i] = server.arg("max" + String(i)).toInt();
    }
    sensorEnabled[i] = server.hasArg("sensor" + String(i));
  }

  bool needRestartMQTT = false;

  if (server.hasArg("mqtt_ip")) {
    strncpy(mqttServer, server.arg("mqtt_ip").c_str(), sizeof(mqttServer) - 1);
    mqttServer[sizeof(mqttServer) - 1] = '\0';
    needRestartMQTT = true;
  }

  if (server.hasArg("mqtt_port")) {
    mqttPort = server.arg("mqtt_port").toInt();
    needRestartMQTT = true;
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

  if (server.hasArg("mqtt_pass")) {
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

  if (server.hasArg("messenger_enable")) {
    messengerEnabled = true;
  } else {
    messengerEnabled = false;
  }

  if (server.hasArg("osc_ip")) {
    strncpy(oscIp, server.arg("osc_ip").c_str(), sizeof(oscIp) - 1);
    oscIp[sizeof(oscIp) - 1] = '\0';
  }

  if (server.hasArg("osc_port")) {
    oscPort = server.arg("osc_port").toInt();
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

  if (server.hasArg("confirm")) {
    confirmTime = server.arg("confirm").toInt();
  }

  int saveFailCount = saveDistanceConfig();

  if (needRestartMQTT) {
    if (mqtt) {
      esp_mqtt_client_stop(mqtt);
      esp_mqtt_client_destroy(mqtt);
      mqtt = NULL;
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
