// Pocket Luna: StickS3 (K150) firmware.
// Luna lives on the stick. The CoreS3 compact tells her what Tanya does and she reacts,
// eats, collects keepsakes and grows. Tilt the stick and she leans; shake it and she plays.
// Front button: pet (double-press: photobooth shutter). Side button: her progress.
#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <Wire.h>
#include <DFRobot_PAJ7620U2.h>
#include "wand_radio.hpp"
#include "pocket_art.hpp"

namespace {
constexpr uint32_t night = 0x1f1a33, plum = 0x3c3356, lilac = 0xe3d4f3, blush = 0xffc3d8, honey = 0xffda8e, paper = 0xfff1da;
constexpr uint32_t rose = 0xd9607f, iris = 0x6d5b8a, white = 0xffffff, shade = 0x241a33;
struct Stage { const char* name; uint32_t from; };
constexpr Stage stages[] = {{"Moon kitten", 0}, {"Star cat", 12}, {"Moon guardian", 36}, {"Crescent queen", 80},
                            {"Starlight angel", 140}, {"Moon goddess", 220}};
constexpr int stageCount = sizeof(stages) / sizeof(stages[0]);
const char* const cheers[] = {"", "Beautiful kanji!", "Task done!", "Correct!", "Say cheese!", "So wise...", "Moon Prism!", "You can do it!"};

M5Canvas frame(&M5.Display);
Preferences store;
uint32_t food = 0, seen = 0, session = 0;
bool synced = false, linked = false, showStats = false, eventKnown = false;
uint16_t eventSeen = 0;
uint8_t keepsakes = 0;       // one bit per kind of thing Tanya has done at least once
uint8_t react = 0;           // what Luna is reacting to right now
uint32_t reactUntil = 0, partyUntil = 0, petUntil = 0, cheeseUntil = 0, levelUpUntil = 0, dizzyUntil = 0;
uint32_t lastActive = 0, lastFed = 0, lastTone = 0, idleAt = 0, idleUntil = 0;
int partyStep = 0, idle = 0;
float lean = 0, shake = 0;
bool restful = false;       // napping: draw less often to save battery
float slideX = 0, slideV = 0;   // where Luna has skidded to, and how fast
float slideY = 0, slideVY = 0, pitch = 0;   // up and down the screen too: she falls whichever way is downhill
uint32_t bumpUntil = 0;
// Which way the Y reading points when the bottom of the screen is downhill. Checked on Tanya's stick.
constexpr float kDownSign = 1.0f;
// Playing with Luna in the air, through the Gesture Unit on the stick's Grove port.
DFRobot_PAJ7620U2 gestures;
bool gestureReady = false, wireUp = false;
uint32_t gestureTry = 0, gestureAt = 0, playUntil = 0, playAt = 0;
enum Play : uint8_t { None, Stroke, Spin, Jump, Loaf, Boop, Bye };
uint8_t play = None;
int spinWay = 1;
const char* const playWords[] = {"", "Purr purr...", "Wheee!", "Hop!", "Nap time", "Boop!", "Come back!"};
void startPlay(uint8_t what, uint32_t now, uint32_t length) {
  play = what; playAt = now; playUntil = now + length; lastActive = now; showStats = false;
  const float tones[] = {0, 330, 880, 1047, 262, 1319, 494};
  M5.Speaker.tone(tones[what], what == Stroke ? 260 : 110);
}
void readGestures(uint32_t now) {
  if (!gestureReady) {                                   // look for the sensor now and then, so it can be plugged in live
    if (now - gestureTry < 10000 && gestureTry) return;   // a quiet check every ten seconds
    gestureTry = now ? now : 1;
    static int attempt = 0;
    int sda = M5.getPin(m5::pin_name_t::port_a_sda), scl = M5.getPin(m5::pin_name_t::port_a_scl);
    if (sda < 0 || scl < 0) { Serial.println("POCKET no Grove pins"); return; }
    M5.Power.setExtOutput(true);                         // the Grove port's 5 V feeds the sensor
    ++attempt;
    sda = 9; scl = 10;                                   // proven on Tanya's stick with the sticker reader
    if (wireUp) Wire.end();
    wireUp = Wire.begin(sda, scl, 100000);
    if (!wireUp) return;
    delay(30);
    // This sensor can ignore the first knock after power-up, so knock twice and then try it regardless.
    if (!wireUp) wireUp = Wire.begin(sda, scl, 100000);
    for (int knock = 0; knock < 3; ++knock) { Wire.beginTransmission(0x73); Wire.endTransmission(); delay(5); }
    const int started = gestures.begin();
    if (started != 0) return;
    gestures.setGestureHighRate(true);
    gestureReady = true;
    Serial.println("POCKET gesture sensor ready");
    return;
  }
  if (now - gestureAt < 60) return;
  gestureAt = now;
  const auto g = gestures.getGesture();
  if (g == DFRobot_PAJ7620U2::eGestureNone) return;
  Serial.printf("POCKET gesture=%d\n", static_cast<int>(g));
  if (g == DFRobot_PAJ7620U2::eGestureClockwise) { spinWay = 1; startPlay(Spin, now, 2200); }
  else if (g == DFRobot_PAJ7620U2::eGestureAntiClockwise) { spinWay = -1; startPlay(Spin, now, 2200); }
  else if (g == DFRobot_PAJ7620U2::eGestureUp) startPlay(Jump, now, 1100);
  else if (g == DFRobot_PAJ7620U2::eGestureDown) startPlay(Loaf, now, 3200);
  else if (g == DFRobot_PAJ7620U2::eGestureForward) startPlay(Boop, now, 1500);
  else if (g == DFRobot_PAJ7620U2::eGestureBackward) startPlay(Bye, now, 1800);
  else startPlay(Stroke, now, 2400);                     // left, right and waves are all a stroke
}

int stageOf(uint32_t f) { int s = 0; for (int i = 0; i < stageCount; ++i) if (f >= stages[i].from) s = i; return s; }
void save() {
  store.begin("lunapet", false);
  store.putUInt("food", food); store.putUInt("seen", seen); store.putBool("synced", synced); store.putUChar("keep", keepsakes);
  store.end();
}
void load() {
  store.begin("lunapet", true);
  food = store.getUInt("food", 0); seen = store.getUInt("seen", 0); synced = store.getBool("synced", false); keepsakes = store.getUChar("keep", 0);
  store.end();
}
void centered(const char* text, int y, uint32_t color, const lgfx::IFont* font) {
  frame.setFont(font); frame.setTextColor(color); frame.setTextDatum(textdatum_t::top_center);
  frame.drawString(text, 67, y);
}
// A caption on a soft dark pill so it reads over the painting.
void caption(const char* text, uint32_t color) {
  frame.setFont(&fonts::DejaVu12);
  const int w = frame.textWidth(text) + 14;
  frame.fillRoundRect(67 - w / 2, 32, w, 20, 10, shade);
  centered(text, 36, color, &fonts::DejaVu12);
}
void icon(const uint16_t* data, int size, int x, int y) { frame.pushImage(x, y, size, size, data, pet::clear); }
// One of Luna's poses, centred at (cx, cy), optionally scaled and turned.
void pose(const uint16_t* data, float cx, float cy, float zoom = 1.0f, float angle = 0) {
  frame.pushImageRotateZoom(cx, cy, pet::cell / 2.0f, pet::cell / 2.0f, angle, zoom, zoom, pet::cell, pet::cell, data, pet::clear);
}
void paint(uint32_t now) {
  const int stage = stageOf(food);
  const bool party = now < partyUntil, petted = now < petUntil, levelUp = now < levelUpUntil, cheese = now < cheeseUntil;
  const bool reacting = now < reactUntil, dizzy = now < dizzyUntil;
  const bool playing = now < playUntil;
  const uint8_t game = playing ? play : static_cast<uint8_t>(None);
  const bool busy = party || petted || cheese || reacting || dizzy || playing;
  const bool asleep = !busy && now - lastActive > 5UL * 60 * 1000;
  const bool hungry = !busy && !asleep && now - lastFed > 25UL * 60 * 1000;
  frame.pushImage(0, 0, 135, 240, asleep || showStats ? pet::sleep : (party || levelUp) ? pet::party : pet::night);   // the big moon behind her
  for (int i = 0; i < 6; ++i) if (keepsakes & (1 << (i + 1))) icon(pet::keepsakes[i], 20, 5 + i * 21, 4);   // her shelf of keepsakes
  frame.fillCircle(5, 203, 3, linked ? honey : iris);                     // lit when the compact is in range
  if (gestureReady) frame.fillCircle(130, 203, 3, rose);                   // gesture sensor found
  if (showStats) {
    frame.fillRoundRect(8, 30, 119, 176, 12, shade);
    centered("Luna", 38, honey, &fonts::DejaVu18);
    centered(stages[stage].name, 62, paper, &fonts::DejaVu12);
    char line[32];
    snprintf(line, sizeof(line), "%lu moons eaten", static_cast<unsigned long>(food)); centered(line, 92, lilac, &fonts::DejaVu12);
    snprintf(line, sizeof(line), "+%lu today", static_cast<unsigned long>(session)); centered(line, 110, blush, &fonts::DejaVu12);
    if (stage + 1 < stageCount) {
      snprintf(line, sizeof(line), "%lu more to grow", static_cast<unsigned long>(stages[stage + 1].from - food));
      centered(line, 136, paper, &fonts::DejaVu12); centered(stages[stage + 1].name, 154, honey, &fonts::DejaVu12);
    } else centered("Fully grown!", 136, honey, &fonts::DejaVu12);
    int found = 0; for (int i = 1; i < 7; ++i) found += (keepsakes >> i) & 1;
    snprintf(line, sizeof(line), "%d of 6 keepsakes", found); centered(line, 176, lilac, &fonts::DejaVu9);
    const int battery = M5.Power.getBatteryLevel();
    if (battery >= 0) { snprintf(line, sizeof(line), "battery %d%%%s", battery, M5.Power.isCharging() ? " charging" : ""); centered(line, 190, battery < 20 ? blush : lilac, &fonts::DejaVu9); }
  } else {
    const bool idling = now < idleUntil && !busy && !asleep;
    const bool skidding = std::fabs(slideV) > 2.2f || std::fabs(slideVY) > 2.2f || now < bumpUntil;
    // Which picture of her to show.
    const uint16_t* art = pet::stages[stage][(now % 4200) < 140 ? 1 : 0];
    if (asleep || game == Loaf) art = pet::stages[stage][3];
    else if (cheese) art = pet::camera;
    else if (reacting) art = pet::reactions[react - 1];
    else if (dizzy) art = pet::idles[4];
    else if (game == Spin) art = pet::reactions[5];
    else if (game == Jump) art = pet::idles[3];
    else if (game == Bye || hungry || (skidding && !busy)) art = pet::stages[stage][4];
    else if (party || petted || game == Stroke || game == Boop) art = pet::stages[stage][2];
    else if (idling && idle >= 1 && idle <= 4) art = pet::idles[idle - 1];
    // Where she is, and how she moves.
    float cx = 67 + slideX, cy = 134 + slideY, zoom = 1.0f, angle = 0;
    cy += asleep ? std::sin(now / 900.0f) * 1.5f : std::sin(now / 300.0f) * 2;
    if (party || (reacting && react == 3)) cy -= std::fabs(std::sin(now / 110.0f)) * 14;
    if (idling && idle == 4) cy -= std::fabs(std::sin((idleUntil - now) / 95.0f)) * 12;
    const float t = playing ? (now - playAt) / static_cast<float>(playUntil - playAt) : 0;
    if (game == Jump) cy -= std::sin(t * 3.1416f) * 46;
    if (game == Spin) angle = t * 720 * spinWay;
    if (game == Boop) zoom = 1.0f + 0.3f * std::sin(t * 3.1416f);
    if (game == Bye) zoom = 1.0f - 0.2f * std::sin(t * 3.1416f);
    if (game == Stroke) cx += std::sin(now / 70.0f) * 2;
    if (dizzy) angle = std::sin(now / 90.0f) * 14;
    if (skidding && !busy) angle = constrain(slideV * 3.0f, -18.0f, 18.0f);      // she leans into the skid
    if (reacting && react == 6) angle = (now - (reactUntil - 4200)) * 0.5f;
    if (levelUp) frame.pushImageRotateZoom(67, 140, 67.5f, 67.5f, now / 20.0f, 1.0f, 1.0f, 135, 135, pet::burst, pet::clear);
    pose(art, cx, cy, zoom, angle);
    if (asleep || game == Loaf) { centered("z", 86, paper, &fonts::DejaVu12); frame.drawString("Z", 92, 70); }
    if (hungry) { frame.fillRoundRect(86, 62, 34, 28, 12, paper); frame.fillCircle(84, 94, 3, paper); icon(pet::treats[4], 16, 95, 68); }   // dreaming of onigiri
    if (party || petted || game == Stroke) for (int i = 0; i < 6; ++i) {   // treats float up
      const int rise = (now / 9 + i * 41) % 150, x = 8 + i * 21 + static_cast<int>(std::sin((now + i * 300) / 200.0f) * 5);
      icon(pet::treats[i % 4], 16, x, 196 - rise);
    }
    if (cheese) caption("Say cheese!", honey);
    else if (levelUp) { caption("Luna grew!", honey); centered(stages[stage].name, 56, paper, &fonts::DejaVu12); }
    else if (playing) caption(playWords[play], honey);
    else if (reacting) caption(cheers[react], react == 7 ? blush : honey);
    else if (dizzy) caption("Wheee!", honey);
    else if (now < bumpUntil) caption("Bonk!", blush);
    else if (skidding) caption("Whoa whoa!", honey);
    else if (party) caption("Yum! Thank you", honey);
    else if (hungry) caption("Feed me a moon?", blush);
    else if (M5.Power.getBatteryLevel() >= 0 && M5.Power.getBatteryLevel() < 12 && !M5.Power.isCharging()) caption("Charge me soon!", blush);
    const uint32_t from = stages[stage].from, to = stage + 1 < stageCount ? stages[stage + 1].from : from + 1;
    const int width = stage + 1 < stageCount ? static_cast<int>(105 * (food - from) / (to - from)) : 105;
    frame.fillRoundRect(10, 208, 115, 28, 9, shade);                       // a dark plate so the bar and name read on the pale scene
    frame.fillRoundRect(16, 213, 103, 7, 3, plum);
    if (width > 0) frame.fillRoundRect(16, 213, std::max(width - 2, 6), 7, 3, honey);
    frame.setFont(&fonts::Font0); frame.setTextColor(paper); frame.setTextDatum(textdatum_t::top_center);
    frame.drawString(stages[stage].name, 67, 224);
  }
  // Save the small battery: dim when she naps, and switch the screen off after a long quiet spell.
  const bool dark = asleep && now - lastActive > 12UL * 60 * 1000;
  if (!dark) frame.pushSprite(0, 0);
  M5.Display.setBrightness(dark ? 0 : asleep ? 30 : 100);
  restful = asleep;
}
void feed(uint32_t amount, uint32_t now) {
  const int before = stageOf(food);
  food += amount; session += amount; lastFed = now;
  partyUntil = now + 2600 + std::min<uint32_t>(amount, 8) * 250; partyStep = 0; lastActive = now;
  if (stageOf(food) > before) levelUpUntil = now + 4500, partyUntil = now + 4500;
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  M5.Display.setRotation(0);
  setCpuFrequencyMhz(80);                   // plenty for Luna, and much kinder to the battery
  if (M5.getBoard() != m5::board_t::board_M5StickS3) {
    M5.Display.print("Pocket Luna needs StickS3 K150");
    while (true) delay(100);
  }
  frame.setColorDepth(16);
  frame.createSprite(135, 240);
  frame.setSwapBytes(true);                 // the art is stored in the processor's own byte order
  load();
  M5.Speaker.begin(); M5.Speaker.setVolume(200);
  if (!radio::begin(wand::kFeed)) Serial.println("POCKET radio failed");
  lastActive = lastFed = millis(); idleAt = millis() + 9000;
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  wand::Feed packet;
  if (radio::take(wand::kFeed, packet)) {
    if (!synced) { synced = true; seen = packet.total; save(); }
    else if (packet.total > seen) {
      const uint32_t amount = std::min<uint32_t>(packet.total - seen, 40);
      seen = packet.total; feed(amount, now); save();
      Serial.printf("POCKET fed=%lu food=%lu stage=%d\n", static_cast<unsigned long>(amount), static_cast<unsigned long>(food), stageOf(food));
    } else if (packet.total < seen) { seen = packet.total; save(); }
    if (!eventKnown) { eventKnown = true; eventSeen = packet.event; }     // do not replay an old event at switch-on
    else if (packet.event != eventSeen) {
      eventSeen = packet.event;
      if (packet.kind >= 1 && packet.kind <= 7) {
        react = packet.kind; reactUntil = now + 4200; lastActive = now; showStats = false;
        if (!(keepsakes & (1 << packet.kind)) && packet.kind != 7) { keepsakes |= 1 << packet.kind; save(); }
        if (packet.kind == 7) M5.Speaker.tone(392, 160);
        Serial.printf("POCKET react=%u\n", packet.kind);
      }
    }
  }
  linked = radio::isPaired() && radio::silence() < 6000;
  if (radio::isPaired() && radio::silence() > 30000) radio::unpair();

  // Tilt and shake.
  float ax = 0, ay = 0, az = 1;
  if (M5.Imu.getAccel(&ax, &ay, &az) && std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az)) {
    lean = 0.85f * lean + 0.15f * constrain(-ax, -1.0f, 1.0f);
    // Tip the stick and she skids all the way across, picks up speed, and bonks the edge.
    const float pull = std::fabs(lean) > 0.07f ? lean : 0;
    slideV = slideV * 0.90f + pull * 2.6f;
    if (!pull) slideV -= slideX * 0.012f;                                 // level again: she pads back to the middle
    slideX += slideV;
    if (std::fabs(slideX) > 30) {
      if (std::fabs(slideV) > 2.5f) { bumpUntil = now + 500; M5.Speaker.tone(196, 60); lastActive = now; }
      slideX = slideX > 0 ? 30 : -30; slideV = -slideV * 0.45f;
    }
    // The same up and down the screen. Held upright, gravity keeps her on her cushion;
    // lay the stick flat and she slides whichever way you tip it, even up to the moon.
    pitch = 0.85f * pitch + 0.15f * constrain(ay * kDownSign, -1.0f, 1.0f);
    const float pullY = std::fabs(pitch) > 0.07f ? pitch : 0;
    slideVY = slideVY * 0.90f + pullY * 2.6f;
    if (!pullY) slideVY -= slideY * 0.012f;                               // level: she pads back to the cushion
    slideY += slideVY;
    if (slideY > 0 || slideY < -58) {
      if (std::fabs(slideVY) > 3.0f) { bumpUntil = now + 500; M5.Speaker.tone(196, 60); lastActive = now; }
      slideY = slideY > 0 ? 0 : -58; slideVY = -slideVY * 0.4f;
    }
    static uint32_t tiltNote = 0;
    if (now - tiltNote > 2500) { tiltNote = now; Serial.printf("POCKET tilt ax=%.2f ay=%.2f az=%.2f\n", ax, ay, az); }
    const float force = std::fabs(std::sqrt(ax * ax + ay * ay + az * az) - 1.0f);
    shake = 0.8f * shake + 0.2f * force;
    if (shake > 0.9f && now > dizzyUntil + 1500) { dizzyUntil = now + 2400; lastActive = now; M5.Speaker.tone(1319, 90); }
  }
  readGestures(now);
  if (M5.BtnA.wasSingleClicked()) { petUntil = now + 1800; lastActive = now; showStats = false; M5.Speaker.tone(520, 70); }
  if (M5.BtnA.wasDoubleClicked()) {
    static uint16_t pokes = static_cast<uint16_t>(esp_random());
    wand::Poke p = {}; wand::stampHeader(p, wand::kPoke); p.seq = ++pokes;
    for (int i = 0; i < 3; ++i) { radio::send(p); delay(15); }
    cheeseUntil = now + 5500; lastActive = now; showStats = false; M5.Speaker.tone(988, 80);
  }
  if (M5.BtnB.wasPressed()) { showStats = !showStats; lastActive = now; }
  // Now and then she does something by herself: looks about, twitches an ear, yawns, hops, swishes her tail.
  if (now > idleAt && now > idleUntil) { idle = 1 + esp_random() % 5; idleUntil = now + (idle == 3 ? 1600 : idle == 4 ? 900 : 2200); idleAt = now + 7000 + esp_random() % 9000; }
  if (now < partyUntil && now - lastTone > 150 && partyStep < 6) {
    const float notes[] = {784, 988, 1175, 988, 1175, 1568};
    M5.Speaker.tone(notes[partyStep++], 120); lastTone = now;
  }
  paint(now);
  while (Serial.available()) {
    static String command;
    const char c = Serial.read();
    if (c != '\n') { if (command.length() < 24) command += c; continue; }
    command.trim();
    if (command == "SHOT") {              // send the current frame, for checking the design from the Mac
      Serial.println("POCKET SCREENSHOT 135 240");
      const uint8_t* bytes = static_cast<const uint8_t*>(frame.getBuffer());
      size_t sent = 0; const size_t total = 135 * 240 * 2; uint32_t progress = millis();
      while (sent < total && millis() - progress < 2000) {
        const int room = Serial.availableForWrite();
        if (room <= 0) { delay(1); continue; }
        const size_t n = Serial.write(bytes + sent, std::min<size_t>(room, total - sent));
        if (n) { sent += n; progress = millis(); }
      }
      Serial.println();
    } else if (command.startsWith("FEED ")) { feed(command.substring(5).toInt(), now); save(); }
    else if (command.startsWith("REACT ")) { react = constrain(command.substring(6).toInt(), 1L, 7L); reactUntil = now + 4200; lastActive = now; }
    else if (command == "STATS") showStats = !showStats;
    command = "";
  }
  delay(restful ? 220 : 40);
}
