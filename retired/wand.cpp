// Luna's moon wand: StickS3 (K150) firmware.
// Tilt the wand left or right to highlight an answer on the CoreS3 compact, then
// press the front button (KEY1) to lock it in. The side button (KEY2) resets neutral.
#include <Arduino.h>
#include <M5Unified.h>
#include "wand_radio.hpp"

namespace {
constexpr uint32_t night = 0x292440, panel = 0x3c3356, lilac = 0xe3d4f3, blush = 0xffc3d8;
constexpr uint32_t honey = 0xffda8e, paper = 0xfff1da, iris = 0x6d5b8a, mint = 0xbfe6c8;

wand::Tilt tilt;
wand::State compact = {};
bool linked = false;
uint8_t presses = 0;
uint16_t seq = 0;
uint32_t lastSend = 0, lastDebug = 0, flashUntil = 0;
float degrees = 0;
// What is on screen, so the display is redrawn only when something changes.
struct Shown {
  int side = 9, stage = -1, selected = 9, correct = -1, stamps = -1;
  bool linked = false, calibrated = false, flash = false;
  bool operator==(const Shown& o) const {
    return side == o.side && stage == o.stage && selected == o.selected && correct == o.correct &&
           stamps == o.stamps && linked == o.linked && calibrated == o.calibrated && flash == o.flash;
  }
} shown;

void centered(const char* text, int y, uint32_t color, const lgfx::IFont* font) {
  auto& s = M5.Display;
  s.setFont(font); s.setTextColor(color); s.setTextDatum(textdatum_t::top_center);
  s.drawString(text, s.width() / 2, y);
}
void moon(int x, int y, int r, uint32_t back) {
  M5.Display.fillCircle(x, y, r, honey);
  M5.Display.fillCircle(x + r / 3, y - r / 3, r * 4 / 5, back);
}
void arrow(bool right, bool lit) {
  auto& s = M5.Display;
  const int w = s.width(), y = 122, x = right ? w - 12 : 12, dir = right ? -1 : 1;
  s.fillTriangle(x, y, x + dir * 40, y - 30, x + dir * 40, y + 30, lit ? blush : panel);
}
void paint() {
  auto& s = M5.Display;
  const int stage = linked ? compact.stage : -1;
  const bool flash = millis() < flashUntil;
  Shown next;
  next.side = tilt.side; next.stage = stage; next.selected = linked ? compact.selected : 9;
  next.correct = linked ? compact.correct : -1; next.stamps = linked ? compact.stamps : -1;
  next.linked = linked; next.calibrated = tilt.calibrated; next.flash = flash;
  if (next == shown) return;
  shown = next;
  s.startWrite();
  s.fillScreen(flash ? iris : night);
  moon(s.width() / 2, 26, 15, flash ? iris : night);
  centered("Luna wand", 48, lilac, &fonts::DejaVu12);
  s.fillCircle(14, 14, 5, linked ? mint : iris);
  if (!tilt.calibrated) {
    centered("Hold me", 92, paper, &fonts::DejaVu18);
    centered("comfortably", 116, paper, &fonts::DejaVu18);
    centered("then press", 152, honey, &fonts::DejaVu12);
    centered("the front button", 170, honey, &fonts::DejaVu12);
  } else {
    arrow(false, tilt.side < 0);
    arrow(true, tilt.side > 0);
    const char* top = "Tilt to choose";
    const char* bottom = "Press to lock in";
    if (!linked) { top = "Looking for"; bottom = "the compact..."; }
    else if (stage == wand::kAway) { top = "Open Japanese"; bottom = "on the compact"; }
    else if (stage == wand::kSetup) { top = "Ready!"; bottom = "Press to begin"; }
    else if (stage == wand::kFeedback) { top = compact.correct ? "Moon stamp!" : "Not quite"; bottom = "Press for next"; }
    else if (stage == wand::kScore) { top = "All done!"; bottom = "Press to replay"; }
    centered(top, 172, stage == wand::kFeedback && compact.correct ? honey : paper, &fonts::DejaVu12);
    centered(bottom, 192, lilac, &fonts::DejaVu12);
    if (linked && stage != wand::kAway)
      for (int i = 0; i < compact.rounds && i < 6; ++i)
      {
        const int x = s.width() / 2 - (compact.rounds - 1) * 9 + i * 18;
        if (i < compact.stamps) moon(x, 224, 6, flash ? iris : night);
        else s.fillCircle(x, 224, 3, iris);
      }
  }
  s.endWrite();
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // debug lines must never delay tilt or button handling
  M5.Display.setRotation(0);
  M5.Display.setBrightness(140);
  if (M5.getBoard() != m5::board_t::board_M5StickS3) {
    M5.Display.print("Luna wand needs StickS3 K150");
    while (true) delay(100);
  }
  if (!radio::begin(wand::kState)) Serial.println("WAND radio failed");
  paint();
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  float ax = 0, ay = 0, az = 1;
  const bool motion = M5.Imu.getAccel(&ax, &ay, &az) && std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az);

  wand::State incoming;
  if (radio::take(wand::kState, incoming)) compact = incoming;
  linked = radio::isPaired() && radio::silence() < 1500;
  if (radio::isPaired() && radio::silence() > 6000) radio::unpair();   // look for a compact again

  if (M5.BtnB.wasPressed() && motion) { tilt.calibrate(ax, ay, az); flashUntil = now + 180; }
  if (M5.BtnA.wasPressed()) {
    flashUntil = now + 120;
    if (!tilt.calibrated) {
      if (motion) tilt.calibrate(ax, ay, az);       // the first press only sets neutral
    } else {
      if (motion && (!linked || compact.stage == wand::kSetup)) tilt.calibrate(ax, ay, az);
      ++presses;
    }
  }
  if (motion && now - lastSend >= 50) {
    lastSend = now;
    degrees = tilt.update(ax, ay, az);
    wand::Motion m = {};
    wand::stampHeader(m, wand::kMotion);
    m.seq = ++seq;
    m.side = static_cast<int8_t>(tilt.side);
    m.degrees = static_cast<int8_t>(constrain(degrees, -120.0f, 120.0f));
    m.presses = presses;
    m.calibrated = tilt.calibrated;
    radio::send(m);
  }
  if (now - lastDebug >= 500) {
    lastDebug = now;
    Serial.printf("WAND linked=%d cal=%d side=%d deg=%d presses=%u ax=%.2f ay=%.2f az=%.2f stage=%u\n",
                  linked, tilt.calibrated, tilt.side, static_cast<int>(degrees), presses, ax, ay, az, compact.stage);
  }
  paint();
  delay(5);
}
