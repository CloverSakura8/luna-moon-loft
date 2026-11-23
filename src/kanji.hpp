#pragma once
// Kanji practice for the CoreS3 touchscreen: Genki I Lesson 5.
// Trace over the guide, or hide it and write from memory, then show it to check.
#include <M5Unified.h>
#include "art.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace kanji {
namespace detail {
constexpr uint32_t night = 0x292440, panel = 0x3c3356, lilac = 0xe3d4f3, blush = 0xffc3d8;
constexpr uint32_t honey = 0xffda8e, paper = 0xfff1da, guide = 0xe6cfc4, grid = 0xf0dcc6, ink = 0x292447;
constexpr int boxX = 8, boxY = 44, boxSize = 188;
struct Entry { const char* glyph; const char* reading; const char* meaning; };
constexpr Entry lesson[] = {
  {"山", "やま", "mountain"}, {"川", "かわ", "river"}, {"元", "げん", "origin"}, {"気", "き", "spirit"},
  {"天", "てん", "heaven"}, {"私", "わたし", "I, me"}, {"今", "いま", "now"}, {"田", "た", "rice field"},
  {"女", "おんな", "woman"}, {"男", "おとこ", "man"}, {"見", "み(る)", "to see"}, {"行", "い(く)", "to go"},
  {"食", "た(べる)", "to eat"}, {"飲", "の(む)", "to drink"}
};
constexpr int total = sizeof(lesson) / sizeof(lesson[0]);
constexpr int maxPoints = 900;
struct State {
  int at = 0;
  bool showGuide = true, drawing = false;
  int16_t px[maxPoints], py[maxPoints];   // x < 0 marks the start of a new stroke
  int points = 0;
  int lastX = 0, lastY = 0;
  uint32_t strokeAt = 0, strokeLoops = 0, strokePoints = 0, strokeDrawUs = 0;   // timing for one stroke
  int moons = 0;            // kanji written well this session
  int tries = 0;            // every press of Check
  bool celebrating = false;
  char note[40] = "";
};
inline State& state() { static State value; return value; }

inline void sideButton(int index, const char* label, uint32_t fill) {
  (void)fill;
  const int y = 84 + index * 52;
  art::draw(art::button108, 108, art::button112H, 204, y);
  art::words(label, 258, y + 24, &fonts::DejaVu18, paper);
}
inline void segment(int x0, int y0, int x1, int y1) {
  auto& s = M5.Display;
  const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) / 2 + 1;
  for (int i = 0; i <= steps; ++i) s.fillCircle(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, 4, ink);
}
inline void star(int x, int y, int r, uint32_t color) {
  auto& s = M5.Display;
  s.fillTriangle(x, y - r, x + r / 2, y, x - r / 2, y, color);
  s.fillTriangle(x, y + r, x + r / 2, y, x - r / 2, y, color);
  s.fillTriangle(x - r, y, x, y - r / 2, x, y + r / 2, color);
  s.fillTriangle(x + r, y, x, y - r / 2, x, y + r / 2, color);
}
inline void moon(int x, int y, int r, uint32_t back) {
  M5.Display.fillCircle(x, y, r, honey);
  M5.Display.fillCircle(x + r / 3, y - r / 3, r * 4 / 5, back);
}
// How well do the strokes match the kanji? `cover` is how much of the character has
// ink on it; `tidy` is how much of the ink sits on the character.
inline bool judge(float& cover, float& tidy) {
  const auto& k = state();
  constexpr int w = 192;
  M5Canvas glyph(&M5.Display), inkMask(&M5.Display);
  glyph.setColorDepth(8); inkMask.setColorDepth(8);
  if (!glyph.createSprite(w, w) || !inkMask.createSprite(w, w)) return false;
  glyph.fillSprite(0); inkMask.fillSprite(0);
  glyph.setFont(&fonts::lgfxJapanGothic_40); glyph.setTextSize(4); glyph.setTextColor(0xFFFFFFu);
  glyph.setTextDatum(textdatum_t::middle_center);
  glyph.drawString(lesson[k.at].glyph, boxSize / 2, boxSize / 2 + 4);
  int px = 0, py = 0;
  for (int i = 0; i < k.points; ++i) {                 // a generous brush: near enough counts
    const bool start = k.px[i] < 0;
    const int x = (start ? -k.px[i] - 2 : k.px[i]) - boxX, y = k.py[i] - boxY;
    if (start) inkMask.fillCircle(x, y, 10, 0xFFFFFFu);
    else {
      const int steps = std::max(std::abs(x - px), std::abs(y - py)) / 4 + 1;
      for (int j = 1; j <= steps; ++j) inkMask.fillCircle(px + (x - px) * j / steps, py + (y - py) * j / steps, 10, 0xFFFFFFu);
    }
    px = x; py = y;
  }
  const uint8_t* g = static_cast<const uint8_t*>(glyph.getBuffer());
  const uint8_t* m = static_cast<const uint8_t*>(inkMask.getBuffer());
  long glyphPixels = 0, covered = 0, inkPixels = 0, strayInk = 0;
  for (int y = 0; y < w; ++y) for (int x = 0; x < w; ++x) {
    const bool onGlyph = g[y * w + x], onInk = m[y * w + x];
    glyphPixels += onGlyph; covered += onGlyph && onInk; inkPixels += onInk;
    if (onInk && !onGlyph) {                           // ink counts as stray only when far from any stroke
      bool near = false;
      for (int d = 6; d <= 18 && !near; d += 6)
        near = (x >= d && g[y * w + x - d]) || (x + d < w && g[y * w + x + d]) ||
               (y >= d && g[(y - d) * w + x]) || (y + d < w && g[(y + d) * w + x]);
      strayInk += !near;
    }
  }
  cover = glyphPixels ? static_cast<float>(covered) / glyphPixels : 0;
  tidy = inkPixels ? 1.0f - static_cast<float>(strayInk) / inkPixels : 0;
  return true;
}
inline void celebrate() {
  auto& s = M5.Display;
  auto& k = state();
  const int cx = boxX + boxSize / 2, cy = boxY + boxSize / 2;
  const bool finale = k.at == total - 1;
  M5.Speaker.begin(); M5.Speaker.setVolume(110);
  const float notes[] = {784, 988, 1175, 1568};
  (void)cx; (void)cy;
  for (int frame = 0; frame < 4; ++frame) {            // moons, stars and hearts shower down over her writing
    M5.Speaker.tone(notes[frame], 110);
    art::drawRle(art::celebrates[frame], 240);
    delay(finale ? 190 : 140);
  }
  art::draw(art::banner, art::bannerW, art::bannerH, 0, 128);       // the ribbon banner
  art::words(finale ? "レッスン５ できた" : "よくできました", 160, 170, &fonts::efontJA_24, static_cast<uint32_t>(0xffda8e));
  char line[48]; snprintf(line, sizeof(line), "%d moon%s  -  tap for next", k.moons, k.moons == 1 ? "" : "s");
  art::words(line, 160, 192, &fonts::Font0, paper);
  art::draw(art::cat, art::catW, art::catH, 112, 34);               // Luna leaps above it
}
inline void canvas() {
  auto& s = M5.Display;
  const auto& k = state();
  s.fillRect(boxX, boxY, boxSize, boxSize, static_cast<uint32_t>(0xfff2dc));     // the desk art's own sheet of paper
  { char page[10]; snprintf(page, sizeof(page), "%d/%d", k.at + 1, total); s.setFont(&fonts::Font0); s.setTextSize(1); s.setTextColor(guide); s.setCursor(boxX + 6, boxY + 6); s.print(page); }
  for (int i = 4; i < boxSize - 4; i += 8) {            // dashed centre lines, like squared practice paper
    s.drawFastHLine(boxX + i, boxY + boxSize / 2, 4, grid);
    s.drawFastVLine(boxX + boxSize / 2, boxY + i, 4, grid);
  }
  if (k.showGuide) {
    s.setFont(&fonts::lgfxJapanGothic_40); s.setTextSize(4); s.setTextColor(guide);
    s.setTextDatum(textdatum_t::middle_center);
    s.drawString(lesson[k.at].glyph, boxX + boxSize / 2, boxY + boxSize / 2 + 4);
    s.setTextDatum(textdatum_t::top_left); s.setTextSize(1);
  }
  s.setClipRect(boxX + 2, boxY + 2, boxSize - 4, boxSize - 4);
  for (int i = 0; i < k.points; ++i) {
    if (k.px[i] < 0) { M5.Display.fillCircle(-k.px[i] - 2, k.py[i], 4, ink); continue; }
    const int fromX = k.px[i - 1] < 0 ? -k.px[i - 1] - 2 : k.px[i - 1];
    segment(fromX, k.py[i - 1], k.px[i], k.py[i]);
  }
  s.clearClipRect();
}
}  // namespace detail

inline void paint() {
  using namespace detail;
  auto& s = M5.Display;
  const auto& k = state();
  art::draw(art::bgKanji, 320, 240, 0, 0, false);       // the calligraphy desk; its blank sheet is the drawing area
  s.fillRoundRect(4, 4, 46, 30, 8, panel);
  s.setFont(&fonts::Font0); s.setTextSize(1); s.setTextColor(lilac);
  s.setCursor(10, 15); s.print("< Back");
  char count[40];
  art::draw(art::stamp, art::stampW, art::stampH, 196, 3);
  snprintf(count, sizeof(count), "x%d", k.moons);
  s.setTextColor(honey); s.setCursor(231, 15); s.print(count);
  s.fillRoundRect(262, 4, 54, 30, 8, panel);
  s.setTextColor(lilac); s.setCursor(270, 15); s.print("Skip >");
  // meaning, large, with the reading above it
  s.setFont(&fonts::efontJA_16); s.setTextColor(honey); s.setCursor(204, 40); s.print(lesson[k.at].reading);
  s.setFont(strlen(lesson[k.at].meaning) > 8 ? &fonts::DejaVu12 : &fonts::DejaVu18);
  s.setTextColor(paper); s.setCursor(204, 60); s.print(lesson[k.at].meaning);
  sideButton(0, "Check", blush);
  sideButton(1, "Clear", lilac);
  sideButton(2, k.showGuide ? "Hide" : "Show", honey);
  canvas();
  s.setFont(&fonts::Font0); s.setTextSize(1);
  if (k.note[0]) {                                     // gentle feedback under the top bar of the canvas
    s.fillRoundRect(boxX + 4, boxY + boxSize - 20, boxSize - 8, 16, 4, blush);
    s.setTextColor(ink); s.setCursor(boxX + 10, boxY + boxSize - 16); s.print(k.note);
  }
}
inline void enter() { auto& k = detail::state(); k.points = 0; k.drawing = false; k.showGuide = true; k.celebrating = false; k.note[0] = 0; paint(); }

// Returns true when the player goes back.
inline bool update() {
  using namespace detail;
  auto& k = state();
  auto t = M5.Touch.getDetail();
  auto advance = [&k]() { k.at = (k.at + 1) % total; k.points = 0; k.showGuide = true; k.note[0] = 0; k.celebrating = false; paint(); };
  if (k.celebrating) {                                 // any tap after the celebration moves on
    if (t.wasPressed()) advance();
    return false;
  }
  if (t.wasPressed()) {
    if (t.x <= 58 && t.y < 40) return true;
    if (t.x >= 256 && t.y < 40) { advance(); return false; }
    if (t.x >= 200 && t.y >= 84) {
      const int button = (t.y - 84) / 52;
      if (button == 0) {
        float cover = 0, tidy = 0;
        const bool judged = k.points > 3 && judge(cover, tidy);
        if (k.points > 3) ++k.tries;
        Serial.printf("LUNA/1 KANJI %d cover=%d tidy=%d points=%d\n", k.at, int(cover * 100), int(tidy * 100), k.points);
        if (judged && cover >= 0.72f && tidy >= 0.8f) { ++k.moons; k.celebrating = true; celebrate(); }
        else {
          snprintf(k.note, sizeof(k.note), "%s", k.points <= 3 ? "Write the kanji first" :
                   cover < 0.72f ? "Almost! Fill in the pale parts" : "Close! Keep to the shape");
          k.showGuide = true; paint();
        }
      }
      else if (button == 1) { k.points = 0; k.note[0] = 0; paint(); }
      else { k.showGuide = !k.showGuide; paint(); }
      return false;
    }
  }
  const bool inside = t.x >= boxX + 4 && t.x < boxX + boxSize - 4 && t.y >= boxY + 4 && t.y < boxY + boxSize - 4;
  if (t.isPressed() && inside) {
    ++k.strokeLoops;
    if (!k.drawing) {
      k.drawing = true;
      k.strokeAt = millis(); k.strokeLoops = 1; k.strokePoints = 0; k.strokeDrawUs = 0;
      if (k.points < maxPoints) { k.px[k.points] = static_cast<int16_t>(-(t.x + 2)); k.py[k.points++] = t.y; }
      M5.Display.fillCircle(t.x, t.y, 4, ink);
    } else if (t.x != k.lastX || t.y != k.lastY) {
      const uint32_t drawBegan = micros();
      M5.Display.setClipRect(boxX + 2, boxY + 2, boxSize - 4, boxSize - 4);
      segment(k.lastX, k.lastY, t.x, t.y);
      M5.Display.clearClipRect();
      k.strokeDrawUs += micros() - drawBegan; ++k.strokePoints;
      if (k.points < maxPoints) { k.px[k.points] = t.x; k.py[k.points++] = t.y; }
    } else return false;
    k.lastX = t.x; k.lastY = t.y;
  } else if (!t.isPressed() || !inside) {
    if (k.drawing && k.strokePoints > 3) {
      const uint32_t ms = millis() - k.strokeAt;
      Serial.printf("LUNA/1 STROKE ms=%lu loops=%lu points=%lu drawus=%lu\n", (unsigned long)ms, (unsigned long)k.strokeLoops,
                    (unsigned long)k.strokePoints, (unsigned long)(k.strokeDrawUs / k.strokePoints));
    }
    k.drawing = false;
  }
  return false;
}
}  // namespace kanji
