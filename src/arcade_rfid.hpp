#pragma once

// Optional kit reader: PlatformIO kkloesener/MFRC522_I2C@1.0.
// poll() is for the Loft home screen. A momentary card tap returns its UID locally.
#include <M5Unified.h>

#if __has_include(<MFRC522_I2C.h>)
#include <Wire.h>
#include <MFRC522_I2C.h>

namespace arcade_rfid {
inline bool poll(char (&uid)[21]) {
  constexpr uint8_t address = 0x28;
  static MFRC522_I2C reader(address, -1);
  static bool busStarted = false, found = false;
  static uint32_t lastPoll = 0;
  const uint32_t now = millis();
  uid[0] = '\0';
  if (now - lastPoll < (found ? 220u : 1500u)) return false;
  lastPoll = now;
  if (!busStarted) {
    int sda = M5.getPin(m5::pin_name_t::port_a_sda);
    int scl = M5.getPin(m5::pin_name_t::port_a_scl);
    if (sda < 0 || scl < 0 || !Wire.begin(sda, scl, 100000)) return false;
    busStarted = true;
  }
  Wire.beginTransmission(address);
  if (Wire.endTransmission() != 0) { found = false; return false; }
  if (!found) {
    reader.PCD_Init();
    const uint8_t version = reader.PCD_ReadRegister(MFRC522_I2C::VersionReg);
    found = version != 0 && version != 0xff;
    if (!found) return false;
  }
  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) return false;
  const auto size = reader.uid.size;
  if (size != 4 && size != 7 && size != 10) { reader.PICC_HaltA(); return false; }
  constexpr char digits[] = "0123456789ABCDEF";
  for (uint8_t i = 0; i < size; ++i) {
    uid[2*i] = digits[reader.uid.uidByte[i] >> 4];
    uid[2*i+1] = digits[reader.uid.uidByte[i] & 0x0f];
  }
  uid[2*size] = '\0';
  reader.PICC_HaltA();
  return true;
}
}  // namespace arcade_rfid
#else
namespace arcade_rfid {
inline bool poll(char (&uid)[21]) { uid[0] = '\0'; return false; }
}  // namespace arcade_rfid
#endif
