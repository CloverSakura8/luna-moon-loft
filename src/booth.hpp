#pragma once
// Moon photobooth for the CoreS3's front camera (GC0308, 320x240).
// The photo stays on the device and the Mac; nothing is sent to any online service.
#include <M5Unified.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include "art.hpp"
#include <cmath>

namespace booth {
constexpr int width = 320, height = 240;
constexpr size_t bytes = width * height * 2;
inline uint8_t* photo = nullptr;     // the last picture, RGB565 as the camera delivers it
inline bool hasPhoto = false;
inline char problem[48] = "";

inline bool cameraOn() {
  M5.In_I2C.release();               // the camera's control lines share the internal I2C bus
  camera_config_t c = {};
  c.pin_pwdn = -1; c.pin_reset = -1;
  c.pin_xclk = -1;                   // the sensor has its own clock; G2 stays free for the RFID reader
  c.pin_sccb_sda = 12; c.pin_sccb_scl = 11;
  c.pin_d7 = 47; c.pin_d6 = 48; c.pin_d5 = 16; c.pin_d4 = 15; c.pin_d3 = 42; c.pin_d2 = 41; c.pin_d1 = 40; c.pin_d0 = 39;
  c.pin_vsync = 46; c.pin_href = 38; c.pin_pclk = 45;
  c.xclk_freq_hz = 20000000; c.ledc_timer = LEDC_TIMER_0; c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_RGB565; c.frame_size = FRAMESIZE_QVGA; c.jpeg_quality = 0;
  const bool roomy = psramFound();
  c.fb_count = roomy ? 2 : 1;
  c.fb_location = roomy ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = roomy ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  c.sccb_i2c_port = 1;
  const esp_err_t result = esp_camera_init(&c);
  if (result != ESP_OK) { snprintf(problem, sizeof(problem), "camera start failed 0x%x", static_cast<unsigned>(result)); return false; }
  if (sensor_t* s = esp_camera_sensor_get()) s->set_hmirror(s, 1);   // mirror, like a selfie
  return true;
}
inline void cameraOff() {
  esp_camera_deinit();
  M5.In_I2C.begin(I2C_NUM_1, 12, 11);   // give the bus back to touch and power
}
inline void moon(int x, int y, int r, uint32_t color, uint32_t back) {
  M5.Display.fillCircle(x, y, r, color); M5.Display.fillCircle(x + r / 3, y - r / 3, r * 4 / 5, back);
}
inline void star(int x, int y, int r, uint32_t color) {
  auto& s = M5.Display;
  s.fillTriangle(x, y - r, x + r / 2, y, x - r / 2, y, color); s.fillTriangle(x, y + r, x + r / 2, y, x - r / 2, y, color);
  s.fillTriangle(x - r, y, x, y - r / 2, x, y + r / 2, color); s.fillTriangle(x + r, y, x, y - r / 2, x, y + r / 2, color);
}
// The keepsake frame drawn over the photo. A plain placeholder: Tanya's own frame art replaces it.
inline void frame() {
  auto& s = M5.Display;
  constexpr uint32_t plum = 0x432a43, gold = 0xffda8e, pink = 0xffc3d8, lilac = 0xe3d4f3;
  s.fillRect(0, 0, 320, 14, plum); s.fillRect(0, 226, 320, 14, plum);
  s.fillRect(0, 0, 10, 240, plum); s.fillRect(310, 0, 10, 240, plum);
  s.drawRect(10, 14, 300, 212, gold); s.drawRect(11, 15, 298, 210, gold);
  for (int i = 0; i < 9; ++i) { star(28 + i * 33, 7, 4, i % 2 ? pink : gold); star(44 + i * 33, 233, 4, i % 2 ? gold : lilac); }
  moon(26, 32, 12, gold, static_cast<uint32_t>(0x000000)); moon(294, 32, 12, gold, static_cast<uint32_t>(0x000000));
  s.setSwapBytes(true);
  s.pushImage(216, 138, art::catW, art::catH, art::cat, art::clear);
  s.pushImage(14, 190, art::stampW, art::stampH, art::stamp, art::clear);
  s.setSwapBytes(false);
}
inline int style = 3;                // which of Tanya's four frames the photo wears; advances each shot
inline void showPhoto() {
  if (!hasPhoto) return;
  M5.Display.pushImage(0, 0, width, height, reinterpret_cast<const uint16_t*>(photo));
  art::drawRle(art::frames[style], 240);
}
// Live preview with a 3-2-1 countdown, then the snap. Blocks for about five seconds.
inline bool cancelled = false;
// `stop` lets the caller cancel too (a second double-press on the stick).
inline bool session(bool (*stop)() = nullptr) {
  cancelled = false;
  auto& s = M5.Display;
  problem[0] = 0;
  if (!photo) photo = static_cast<uint8_t*>(heap_caps_malloc(bytes, psramFound() ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : MALLOC_CAP_8BIT));
  if (!photo) { snprintf(problem, sizeof(problem), "no memory for a photo"); return false; }
  s.fillScreen(static_cast<uint32_t>(0x2a1a33));
  art::drawRle(art::frames[(style + 1) % 4], 240);                    // a peek at the frame this photo will wear
  art::words("Moon photobooth", 160, 96, &fonts::DejaVu18, static_cast<uint32_t>(0xffda8e));
  art::words("Step back and strike a pose!", 160, 126, &fonts::DejaVu12, static_cast<uint32_t>(0xfff1da));
  art::words("Touch the screen to cancel", 160, 150, &fonts::DejaVu9, static_cast<uint32_t>(0xe3d4f3));
  if (!cameraOn()) { cameraOff(); return false; }
  // The camera only needs the shared bus while it starts, so hand it back and keep reading touch.
  M5.In_I2C.begin(I2C_NUM_1, 12, 11);
  M5.update();                          // forget the tap that opened the booth
  const bool heldAtStart = M5.Touch.getDetail().isPressed();
  bool released = !heldAtStart;
  M5.Speaker.begin(); M5.Speaker.setVolume(130);
  const uint32_t began = millis();
  int spoken = 4; bool taken = false;
  while (!taken) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { if (millis() - began > 6000) { snprintf(problem, sizeof(problem), "camera sent no picture"); break; } continue; }
    const uint32_t elapsed = millis() - began;
    M5.update();
    const bool finger = M5.Touch.getDetail().isPressed();
    if (!finger) released = true;
    if ((finger && released && elapsed > 400) || (stop && stop())) { cancelled = true; esp_camera_fb_return(fb); break; }
    const int left = 3 - static_cast<int>((elapsed > 700 ? elapsed - 700 : 0) / 1000);   // a moment to settle, then 3, 2, 1
    if (left <= 0 && fb->len >= bytes) {
      memcpy(photo, fb->buf, bytes); hasPhoto = true; taken = true;
    } else {
      s.pushImage(0, 0, width, height, reinterpret_cast<const uint16_t*>(fb->buf));
      if (elapsed > 700 && left >= 1) {
        art::draw(art::medals[3 - left], 120, 120, 100, 60);          // the moon medallion
        s.setFont(&fonts::DejaVu56); s.setTextColor(static_cast<uint32_t>(0xffda8e)); s.setTextDatum(textdatum_t::middle_center);
        s.drawNumber(left, 160, 123); s.setTextDatum(textdatum_t::top_left);
        if (left < spoken) { spoken = left; M5.Speaker.tone(left == 1 ? 1175 : 880, 140); }
      }
    }
    esp_camera_fb_return(fb);
  }
  M5.In_I2C.release();
  cameraOff();
  s.setFont(&fonts::Font0);
  if (cancelled) { M5.Speaker.tone(440, 90); return false; }
  if (!taken) return false;
  art::draw(art::boothFlash, 320, 240, 0, 0, false);                  // a burst of starlight, and the shutter
  M5.Speaker.tone(1568, 90); delay(110); M5.Speaker.tone(2093, 160);
  delay(170);
  style = (style + 1) % 4;
  showPhoto();
  return true;
}
}  // namespace booth
