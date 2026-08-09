#pragma once

void handleData();
void handleSave();
// MOT route duy nhat cho ca 2 kenh: triggerFull() ban ca MQTT lan OSC nen 2 handler rieng
// truoc day chi khac nhau dung 1 dong LOG.
void handleTestIot();
void handleTestRelay();

// OTA firmware update qua Web UI (Update.h, khong can thu vien ngoai) - handleUpdateUpload()
// nhan tung chunk file trong luc upload, handleUpdateFinish() chay 1 lan sau khi nhan xong.
void handleUpdateUpload();
void handleUpdateFinish();

// OTA tu URL da luu (NVS "ota_url"): 2 nut cung form - "Luu URL" chi ghi NVS, "Nap tu link"
// ghi NVS roi dat otaUrlPending de otaUrlTick() trong loop() tai ve. Xem globals.h.
void handleUpdateUrl();

// Reset mem board qua Web UI (ESP.restart()). Gated giong cac route doi trang thai khac.
void handleReboot();
