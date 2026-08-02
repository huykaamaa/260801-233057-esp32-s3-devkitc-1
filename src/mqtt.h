#pragma once

#include <esp_event.h>

void mqttInit();
void triggerFull();
void triggerMissing();
void mqttEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
