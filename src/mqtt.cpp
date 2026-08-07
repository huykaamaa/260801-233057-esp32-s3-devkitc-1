#include "globals.h"
#include "mqtt.h"
#include <cstring>

static void writeOscString(WiFiUDP &udp, const char *text) {
  size_t len = strlen(text);
  udp.write(reinterpret_cast<const uint8_t*>(text), len);
  // OSC 1.0 requires the string to be NUL-terminated then zero-padded to a
  // 4-byte boundary - at least 1 pad byte always, even when len is already
  // a multiple of 4 (F10: "% 4" outer wrap previously produced 0 padding
  // for such lengths, which dropped the terminator entirely).
  size_t padding = 4 - (len % 4);
  for (size_t i = 0; i < padding; ++i) {
    udp.write((uint8_t)0);
  }
}

static void sendOscValue(const char *address, int value) {
  if (!oscEnabled || strlen(oscIp) == 0 || oscPort == 0) {
    return;
  }

  bool hasAddress = (address != nullptr) && (strlen(address) > 0);
  // 4.9: OSC 1.0 address patterns must start with '/'. Primary validation
  // (with operator feedback) happens at Save time in web.cpp handleSave();
  // this is a cheap defensive last line of defense against any other caller.
  if (hasAddress && address[0] != '/') {
    return;
  }

  if (!oscUdp.beginPacket(oscIp, oscPort)) {
    return;
  }

  const char *osc_address = hasAddress ? address : "/sensor/state";
  writeOscString(oscUdp, osc_address);
  writeOscString(oscUdp, ",i");

  // F9: OSC 1.0 requires int32 arguments in big-endian ("network byte
  // order"). The native int is little-endian on ESP32, so byte-swap
  // manually before writing (avoids relying on htonl() include ordering).
  uint32_t be = static_cast<uint32_t>(value);
  uint8_t bytes[4] = {
    static_cast<uint8_t>(be >> 24),
    static_cast<uint8_t>(be >> 16),
    static_cast<uint8_t>(be >> 8),
    static_cast<uint8_t>(be)
  };
  oscUdp.write(bytes, sizeof(bytes));
  oscUdp.endPacket();
}

void sendOscState(const char *address, int value) {
  sendOscValue(address, value);
}

void mqttInit() {
  // mqttServer rong -> URI thanh "mqtt://:1883", esp_mqtt_client_init() that bai va MQTT chet
  // han cho toi lan Save sau. F33 trong handleSave() da chan khong cho luu IP rong nen thuc te
  // khong voi toi day duoc, nhung dat them lop nay cho dong bo voi dat_the/gia_sach.
  // KHONG chan theo mqttEnabled: bo tick "Enable MQTT" chi chan publish (xem triggerFull()),
  // client van ket noi toi broker nhu binh thuong.
  if (strlen(mqttServer) == 0) {
    mqttConnected = false;
    LOG("MQTT: dia chi broker rong - khong khoi tao client");
    return;
  }

  static char uriBuf[96];
  snprintf(uriBuf, sizeof(uriBuf), "mqtt://%s:%u", mqttServer, mqttPort);

  esp_mqtt_client_config_t config;
  memset(&config, 0, sizeof(config));
  config.broker.address.uri = uriBuf;

  static char client_id[] = "CAN_TIM";
  config.credentials.client_id = client_id;

  // F32: the Web UI Password field no longer round-trips the saved value (blank submit =
  // keep current, see web.cpp handleSave), so there is no way to clear mqttPass back to ""
  // through that field anymore. Tying password-sending to username instead gives the
  // operator an escape hatch: clearing Username switches the broker connection fully
  // anonymous (no username, no password sent), even if a password is still stored in NVS.
  if (strlen(mqttUser) > 0) {
    config.credentials.username = mqttUser;
    if (strlen(mqttPass) > 0) {
      config.credentials.authentication.password = mqttPass;
    }
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
    if (mqtt && mqttConnected) {
      // F14/4.2: esp_mqtt_client_publish() blocks on the esp-mqtt internal
      // api_lock with an INFINITE wait, which can stall loop() for seconds
      // during a half-open/reconnecting broker state. esp_mqtt_client_enqueue()
      // queues the message and returns without waiting on that lock; store=true
      // so the QoS-0 payload is still enqueued (by default only QoS 1/2 are).
      esp_mqtt_client_enqueue(mqtt, mqttTopic, mqttFullValue, 0, 0, 0, true);
    } else if (mqtt) {
      // F3: client handle exists but MQTT_EVENT_CONNECTED hasn't fired (or a
      // disconnect already did) - publishing here would either silently drop
      // the message (esp-mqtt returns -1 for QoS-0 publish while disconnected)
      // or block on api_lock, so skip and log instead of pretending it sent.
      LOG("MQTT publish skipped: not connected");
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
    if (mqtt && mqttConnected) {
      // See triggerFull() above for why enqueue() (non-blocking) is used
      // instead of publish() (blocking on api_lock) - F14/4.2.
      esp_mqtt_client_enqueue(mqtt, mqttTopic, mqttMissingValue, 0, 0, 0, true);
    } else if (mqtt) {
      // F3: guard against publishing while not connected - see triggerFull().
      LOG("MQTT publish skipped: not connected");
    } else {
      LOG("MQTT publish skipped: client not initialized");
    }
  } else {
    LOG("MQTT publish skipped: disabled by config");
  }
  sendOscState(oscAddressMissing, oscValueMissing);
}

void triggerSensorOffline(int deviceId) {
  // Topic/payload co dinh (khong qua Web UI/NVS) - chi la 1 canh bao ky thuat, khong
  // phai trang thai occupancy nen KHONG dung chung topic voi FULL/MISSING (mqttTopic)
  // de tranh consumer phia duoi hieu nham la 1 gia tri occupancy thu 3.
  char topicBuf[80];
  snprintf(topicBuf, sizeof(topicBuf), "%s/error", mqttTopic);

  char payloadBuf[32];
  snprintf(payloadBuf, sizeof(payloadBuf), "SENSOR_%d_OFFLINE", deviceId);

  LOG("Sensor %d OFFLINE - gui canh bao MQTT (%s -> %s)", deviceId, topicBuf, payloadBuf);
  if (mqttEnabled) {
    if (mqtt && mqttConnected) {
      // Xem triggerFull() ve ly do dung enqueue() thay vi publish() (F14/4.2).
      esp_mqtt_client_enqueue(mqtt, topicBuf, payloadBuf, 0, 0, 0, true);
    } else if (mqtt) {
      LOG("MQTT publish skipped: not connected");
    } else {
      LOG("MQTT publish skipped: client not initialized");
    }
  } else {
    LOG("MQTT publish skipped: disabled by config");
  }
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
