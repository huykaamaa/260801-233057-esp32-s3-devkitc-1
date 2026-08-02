#include "globals.h"
#include "mqtt.h"
#include <cstring>

static void writeOscString(WiFiUDP &udp, const char *text) {
  size_t len = strlen(text);
  udp.write(reinterpret_cast<const uint8_t*>(text), len);
  size_t padding = (4 - (len % 4)) % 4;
  for (size_t i = 0; i < padding; ++i) {
    udp.write((uint8_t)0);
  }
}

static void sendOscValue(const char *address, int value) {
  if (!oscEnabled || strlen(oscIp) == 0 || oscPort == 0) {
    return;
  }

  if (!oscUdp.beginPacket(oscIp, oscPort)) {
    return;
  }

  const char *osc_address = (strlen(address) > 0) ? address : "/sensor/state";
  writeOscString(oscUdp, osc_address);
  writeOscString(oscUdp, ",i");
  oscUdp.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
  oscUdp.endPacket();
}

void sendOscState(const char *address, int value) {
  sendOscValue(address, value);
}

void mqttInit() {
  static char uriBuf[96];
  snprintf(uriBuf, sizeof(uriBuf), "mqtt://%s:%u", mqttServer, mqttPort);

  esp_mqtt_client_config_t config;
  memset(&config, 0, sizeof(config));
  config.broker.address.uri = uriBuf;

  static char client_id[] = "CAN_TIM";
  config.credentials.client_id = client_id;

  if (strlen(mqttUser) > 0) {
    config.credentials.username = mqttUser;
  }

  if (strlen(mqttPass) > 0) {
    config.credentials.authentication.password = mqttPass;
  }

  mqtt = esp_mqtt_client_init(&config);
  if (mqtt) {
    esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqttEvent, NULL);
    esp_mqtt_client_start(mqtt);
  } else {
    mqttConnected = false;
    LOG("MQTT init failed");
  }
}

void triggerFull() {
  LOG(">>> FULL 3 PEOPLE <<<");
  if (mqttEnabled) {
    if (mqtt) {
      esp_mqtt_client_publish(mqtt, mqttTopic, mqttFullValue, 0, 0, 0);
    } else {
      LOG("MQTT publish skipped: client not initialized");
    }
  } else {
    LOG("MQTT publish skipped: disabled by config");
  }
  sendOscState(oscAddressFull, oscValueFull);
}

void triggerMissing() {
  LOG(">>> NOT ENOUGH PEOPLE <<<");
  if (mqttEnabled) {
    if (mqtt) {
      esp_mqtt_client_publish(mqtt, mqttTopic, mqttMissingValue, 0, 0, 0);
    } else {
      LOG("MQTT publish skipped: client not initialized");
    }
  } else {
    LOG("MQTT publish skipped: disabled by config");
  }
  sendOscState(oscAddressMissing, oscValueMissing);
}

void mqttEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      mqttConnected = true;
      LOG("MQTT Connected");
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttConnected = false;
      LOG("MQTT Disconnected");
      break;
    default:
      break;
  }
}
