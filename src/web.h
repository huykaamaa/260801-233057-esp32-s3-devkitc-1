#pragma once

void handleData();
void handleSave();
void handleTestMQTT();
void handleTestOSC();
void handleTestRelay();

// OTA firmware update qua Web UI (Update.h, khong can thu vien ngoai) - handleUpdateUpload()
// nhan tung chunk file trong luc upload, handleUpdateFinish() chay 1 lan sau khi nhan xong.
void handleUpdateUpload();
void handleUpdateFinish();
