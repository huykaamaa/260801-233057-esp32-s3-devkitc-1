#pragma once

// Dieu khien 4 chan GPIO co dinh (4,5,6,7) de kich relay reset nguon cac node ve tinh RS485
// khi 1 sensor bao OFFLINE (xem checkDistance() trong cantim_mqtt_new.cpp). Cac bien cau hinh
// (relayPinEnabled/relayActiveHigh/relayPulseMs) khai bao o globals.h, dinh nghia + load/save
// NVS tap trung o cantim_mqtt_new.cpp giong moi field config khac trong project nay.

void relayInit();          // pinMode + dua ca 4 chan ve muc nghi, goi 1 lan trong setup()
void relaySyncIdleLevel(); // Ap lai muc nghi cho ca 4 chan - goi sau khi Save doi pin/active-high
                            // tren Web UI, de khong bi ket o muc kich cu neu doi dinh nghia active
                            // muc nao. Bo qua (khong lam gi) neu dang giua 1 xung that.
void relayTrigger();        // Kich 1 xung reset qua cac chan da tick chon. Khong lam gi neu
                            // khong chan nao duoc chon, hoac dang giua 1 xung roi (khong chong
                            // xung lien tuc khi nhieu sensor cung bao offline gan nhau).
void relayTick();           // Goi moi vong loop() - tu nha xung ve muc nghi sau relayPulseMs.
