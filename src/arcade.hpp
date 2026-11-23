#pragma once

#include <M5Unified.h>
#include <cmath>
#include <cstdio>
#include "wand_radio.hpp"
#include "art.hpp"

namespace arcade {
namespace detail {
constexpr uint32_t night = 0x292440, panel = 0x3c3356, iris = 0x6d5b8a;
constexpr uint32_t lilac = 0xe3d4f3, blush = 0xffc3d8, honey = 0xffda8e;
constexpr uint32_t paper = 0xfff1da;
constexpr int cardTop = 124, cardBottom = 174, buttonTop = 185, buttonBottom = 232;

struct Card {
  const char* clue;
  const char* phrase;
  const char* choices[2];
  int answer;
  const char* example;
  const char* why;
};
constexpr Card deck[] = {
  {"Say you like sushi.", "すし ___ 好きです。", {"が", "を"}, 0,
   "すしが好きです。", "Use ga before suki to name what you like."},
  {"Yesterday's hike was fun.", "きのうは ___ です。", {"楽しい", "楽しかった"}, 1,
   "きのうは楽しかったです。", "For an i-adjective, change final i to katta."},
  {"Describe a quiet inn.", "___ 旅館です。", {"静かな", "静かい"}, 0,
   "静かな旅館です。", "Before a noun, shizuka takes na."},
  {"Invite a friend to eat.", "いっしょに ___ 。", {"食べました", "食べましょう"}, 1,
   "いっしょに食べましょう。", "Use the verb stem plus mashou to say let's."},
  {"The sea was beautiful.", "海は ___ 。", {"きれいかったです", "きれいでした"}, 1,
   "海はきれいでした。", "Kirei is a na-adjective. Its past uses deshita."}
};
constexpr int rounds = sizeof(deck) / sizeof(deck[0]);

enum class Stage { Center, Right, Question, Feedback, Score };
struct Game {
  Stage stage = Stage::Center;
  int round = 0, score = 0, selected = -1;
  bool tilt = false, tiltArmed = false, fresh = false, calibrateAgain = false;
  float centerX = 0, centerY = 0, axisX = 0, axisY = 0;
  float latestX = 0, latestY = 0, projection = 0;
  uint32_t lastSample = 0, lastGood = 0;
  uint32_t holdSince = 0;   // a tilt held this long grades itself
  bool nextArmed = false;
  uint32_t answerShownAt = 0, levelSince = 0;   // the answer page waits for a separate, deliberate flick
  // StickS3 wand
  wand::Chooser chooser;
  wand::Motion wandNow = {};
  bool wandLinked = false, wandPress = false;
  uint32_t lastState = 0;
};
inline Game& game() {
  static Game value;
  return value;
}

inline void text(const char* value, int x, int y, uint32_t color, int size = 1, bool japanese = false) {
  auto& screen = M5.Display;
  screen.setFont(japanese ? static_cast<const lgfx::IFont*>(&fonts::efontJA_16)
                          : static_cast<const lgfx::IFont*>(&fonts::Font0));
  screen.setTextSize(size);
  screen.setTextColor(color);
  screen.setCursor(x, y);
  screen.print(value);
}
inline void star(int x, int y, int radius, uint32_t color) {
  M5.Display.fillTriangle(x, y-radius, x+radius/2, y, x-radius/2, y, color);
  M5.Display.fillTriangle(x, y+radius, x+radius/2, y, x-radius/2, y, color);
  M5.Display.fillTriangle(x-radius, y, x, y-radius/2, x, y+radius/2, color);
  M5.Display.fillTriangle(x+radius, y, x, y-radius/2, x, y+radius/2, color);
}
inline void moon(int x, int y, int radius, uint32_t back) {
  M5.Display.fillCircle(x, y, radius, honey);
  M5.Display.fillCircle(x + radius/3, y - radius/3, radius*4/5, back);
}
inline void button(int x, int width, const char* label, uint32_t fill, uint32_t color) {
  // Plaque art where a width exists for it; a plain button otherwise.
  const uint16_t* plaque = width >= 300 ? art::button300 : width >= 146 ? art::button148 : nullptr;
  const int w = width >= 300 ? 300 : 148;
  if (!plaque) { M5.Display.fillRoundRect(x, buttonTop, width, buttonBottom-buttonTop, 10, fill); text(label, x+12, buttonTop+17, color, 2); return; }
  (void)fill; (void)color;
  art::draw(plaque, w, art::button112H, x + (width - w) / 2, buttonTop);
  art::words(label, x + width / 2, buttonTop + art::button112H / 2 + 1, &fonts::DejaVu18, paper);
}
// Tanya's pixel art, drawn with its transparent pixels left alone.
inline void sprite(const uint16_t* data, int w, int h, int x, int y) {
  M5.Display.setSwapBytes(true);
  M5.Display.pushImage(x, y, w, h, data, art::clear);
  M5.Display.setSwapBytes(false);
}
inline void wandChip() {
  const bool on = game().wandLinked;
  if (!on) return;   // the tilt wand is retired; show nothing unless one ever connects
  M5.Display.fillRoundRect(206, 8, 74, 24, 8, on ? honey : panel);
  M5.Display.fillCircle(218, 20, 4, on ? night : iris);
  text(on ? "WAND ON" : "NO WAND", 228, 16, on ? night : iris);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
}
inline void hint() {
  const auto& g = game();
  text(g.wandLinked ? "Tilt wand, press its button" : g.tilt ? "Tilt or tap a choice" : "Tap a choice",
       g.wandLinked ? 152 : 190, 45, lilac);
}
inline void top() {
  art::draw(art::bgQuiz, 320, 240, 0, 0, false);       // the starry stage
  M5.Display.fillRoundRect(6, 3, 66, 33, 8, panel);
  text("< Back", 12, 16, lilac);
  wandChip();
}
inline void choices() {
  const auto& g = game();
  const auto& card = deck[g.round];
  for (int i = 0; i < 2; ++i) {
    int x = i == 0 ? 8 : 164;
    bool selected = g.selected == i;
    uint32_t fill = selected ? blush : panel;
    M5.Display.fillRoundRect(x, cardTop, 148, cardBottom-cardTop, 9, fill);
    if (selected) star(x+13, cardTop+11, 5, night);
    text(card.choices[i], x+8, cardTop+21, selected ? night : paper, 1, true);
  }
  button(8, 304, g.selected == -1 ? (g.tilt ? "Tilt to answer" : "Choose an answer") : "Confirm answer",
         g.selected == -1 ? panel : honey, g.selected == -1 ? lilac : night);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
}
inline void roundStart() {
  auto& g = game();
  g.stage = Stage::Question;
  g.selected = -1;
  g.tiltArmed = false;
  g.projection = 0;
  g.holdSince = 0;
  g.nextArmed = false;
  g.answerShownAt = 0;
  g.levelSince = 0;
  g.chooser.newQuestion();
}
// Tell the wand what the compact is showing.
inline void tellWand(uint32_t now, bool force = false) {
  auto& g = game();
  if (!force && now - g.lastState < 200) return;
  g.lastState = now;
  if (!radio::isPaired()) return;
  wand::State s = {};
  wand::stampHeader(s, wand::kState);
  s.stage = g.stage == Stage::Question ? wand::kQuestion : g.stage == Stage::Feedback ? wand::kFeedback :
            g.stage == Stage::Score ? wand::kScore : wand::kSetup;
  s.selected = static_cast<int8_t>(g.selected);
  s.correct = g.stage == Stage::Feedback && g.selected == deck[g.round].answer;
  s.stamps = static_cast<uint8_t>(g.score);
  s.round = static_cast<uint8_t>(g.round);
  s.rounds = rounds;
  radio::send(s);
}
// Read the newest wand packet. Returns true when the link went up or down.
inline bool listenWand(uint32_t now) {
  auto& g = game();
  g.wandPress = false;
  wand::Motion m;
  if (radio::take(wand::kMotion, m)) {
    g.wandNow = m;
    g.wandPress = g.chooser.pressed(m);
  }
  const bool linked = radio::isPaired() && radio::silence() < 1500;
  if (radio::isPaired() && radio::silence() > 12000) radio::unpair();   // let another wand join
  if (linked == g.wandLinked) return false;
  g.wandLinked = linked;
  if (!linked) g.chooser.known = false;
  return true;
}
inline bool sample(uint32_t now) {
  auto& g = game();
  if (now - g.lastSample < 40) return false;
  g.lastSample = now;
  float x, y, z;
  if (!M5.Imu.getAccel(&x, &y, &z) || !std::isfinite(x) || !std::isfinite(y)) return false;
  g.latestX = x;
  g.latestY = y;
  g.lastGood = now;
  g.fresh = true;
  return true;
}
}  // namespace detail

inline void paint() {
  using namespace detail;
  const auto& g = game();
  top();
  if (g.stage == Stage::Center || g.stage == Stage::Right) {
    const bool center = g.stage == Stage::Center;
    text("Luna's tilt arcade", 15, 46, paper, 2);
    M5.Display.fillRoundRect(8, 73, 304, 99, 10, lilac);
    M5.Display.drawRoundRect(8, 73, 304, 99, 10, honey);
    moon(36, 101, 13, lilac);
    text(center ? "1. Set your center" : "2. Set your right", 63, 92, night, 2);
    if (g.wandLinked) {
      M5.Display.fillRoundRect(8, 73, 304, 99, 10, honey);
      moon(36, 101, 13, honey);
      text("Wand connected!", 63, 92, night, 2);
      text("Hold your wand comfortably, then press", 18, 128, night);
      text("its front button to begin.", 18, 147, night);
    } else {
    text(center ? "Hold your compact comfortably level." : "Lower the right edge and hold it there.", 18, 128, night);
    text(g.calibrateAgain ? (center ? "Waiting for the sensor. Touch mode is ready." :
                                     "Tip a little farther, then tap Set right.")
                          : (center ? "Tap Set center when you are ready." :
                                      "Tap Set right while the compact is tilted."), 18, 147, night);
    }
    button(8, 146, "Touch only", lilac, night);
    button(163, 149, center ? "Set center" : "Set right", blush, night);
  } else if (g.stage == Stage::Score) {
    text("Your moon stamps", 17, 48, paper, 2);
    M5.Display.fillRoundRect(8, 80, 304, 92, 10, panel);
    for (int i = 0; i < rounds; ++i)
      sprite(i < g.score ? art::stamp : art::stampEmpty, art::stampW, art::stampH, 42+51*i, 94);
    char score[34];
    snprintf(score, sizeof(score), "%d / %d correct", g.score, rounds);
    text(score, 30, 146, paper, 2);
    button(8, 146, "Back", lilac, night);
    button(163, 149, "Replay", blush, night);
  } else {
    const auto& card = deck[g.round];
    char number[18];
    snprintf(number, sizeof(number), "CARD %d / %d", g.round+1, rounds);
    text(number, 15, 45, lilac);
    if (g.stage == Stage::Question) {
      hint();
      text(card.clue, 15, 67, paper, 2);
      text(card.phrase, 15, 96, honey, 1, true);
      choices();
    } else {
      bool correct = g.selected == card.answer;
      text(correct ? "Moon stamp!" : "Try saying it:", 15, 66, correct ? honey : blush, 2);
      sprite(correct ? art::quizYes : art::quizNo, art::quizYesW, art::quizYesH, 234, 8);      // Luna cheers, or tilts her head kindly
      char stamps[20];
      snprintf(stamps, sizeof(stamps), "STAMPS %d", g.score);
      text(stamps, 150, 45, lilac);
      M5.Display.fillRoundRect(8, 94, 304, 79, 9, paper);
      text(card.example, 18, 105, night, 1, true);
      text(card.why, 18, 148, night);
      button(8, 304, g.round+1 == rounds ? "See my stamps" : g.tilt ? "Tilt or tap: next" : "Next card", blush, night);
    }
  }
  // Other Loft screens use the default ASCII font.
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
}

inline void enter() {
  using namespace detail;
  game() = Game{};
  game().lastSample = millis() - 40;
  if (!M5.Imu.isEnabled()) roundStart();
  paint();
}

inline bool update(bool tapped, int x, int y) {
  using namespace detail;
  auto& g = game();
  if (tapped && x >= 0 && x <= 74 && y >= 0 && y < 39) return true;
  bool primary = tapped && y >= buttonTop && y <= buttonBottom && x >= 8 && x < 312;
  uint32_t now = millis();
  const Stage before = g.stage;
  const int selectedBefore = g.selected;
  if (listenWand(now)) {
    if (g.stage == Stage::Center || g.stage == Stage::Right) paint();
    else { wandChip(); if (g.stage == Stage::Question) hint(); }
  }
  const bool press = g.wandPress && g.wandLinked;
  if (g.stage == Stage::Center || g.stage == Stage::Right) {
    sample(now);
    if (press) {                       // the wand set its own neutral on this press
      roundStart();
      paint();
      tellWand(now, true);
      return false;
    }
    if (!primary) { tellWand(now); return false; }
    if (x < 154) {
      roundStart();
    } else if (x < 163) {
      return false;
    } else if (!g.fresh || now-g.lastGood > 180) {
      g.calibrateAgain = true;
    } else if (g.stage == Stage::Center) {
      g.centerX = g.latestX;
      g.centerY = g.latestY;
      g.stage = Stage::Right;
      g.calibrateAgain = false;
    } else {
      // Learn the user's physical right in the sensor XY plane, including its sign.
      float dx = g.latestX-g.centerX, dy = g.latestY-g.centerY;
      float excursion = std::sqrt(dx*dx + dy*dy);
      if (excursion >= 0.20f) {
        g.axisX = dx / excursion;
        g.axisY = dy / excursion;
        g.tilt = true;
        roundStart();
      } else {
        g.calibrateAgain = true;
      }
    }
    paint();
  } else if (g.stage == Stage::Question) {
    if (tapped && y >= cardTop && y < cardBottom && ((x >= 8 && x < 156) || (x >= 164 && x < 312))) {
      g.selected = x < 156 ? 0 : 1;
      // Leave the manual choice selected until the user centers and tilts again.
      g.tiltArmed = false;
      choices();
    } else if ((primary || press) && g.selected >= 0) {
      if (g.selected == deck[g.round].answer) ++g.score;
      g.stage = Stage::Feedback;
      g.nextArmed = false;
      paint();
    } else if (g.wandLinked && (g.chooser.selected = g.selected, g.chooser.motion(g.wandNow))) {
      g.selected = g.chooser.selected;
      g.tiltArmed = false;
      choices();
    } else if (g.tilt && sample(now)) {
      float tilt = (g.latestX-g.centerX)*g.axisX + (g.latestY-g.centerY)*g.axisY;
      g.projection = 0.55f*g.projection + 0.45f*tilt;
      if (std::fabs(tilt) < 0.10f && std::fabs(g.projection) < 0.10f) g.tiltArmed = true;
      if (g.tiltArmed && std::fabs(g.projection) >= 0.20f) {
        g.selected = g.projection < 0 ? 0 : 1;
        g.tiltArmed = false;
        g.holdSince = now;
        choices();
      }
      // Keep the tilt held for a moment and the answer grades itself: no Confirm tap needed.
      // Levelling out before the bar fills cancels, so she can still change her mind.
      constexpr uint32_t holdFor = 90;   // near-instant; Tanya found 650 ms too slow
      if (g.holdSince) {
        const bool stillHeld = std::fabs(g.projection) >= 0.12f && (g.projection < 0 ? 0 : 1) == g.selected;
        if (!stillHeld) {
          g.holdSince = 0;
          M5.Display.fillRect(8, cardBottom + 3, 304, 5, 0x2a1a33u);
        } else if (now - g.holdSince >= holdFor) {
          g.holdSince = 0;
          if (g.selected == deck[g.round].answer) ++g.score;
          g.stage = Stage::Feedback;
          g.nextArmed = false;
          paint();
        } else {
          const int x = g.selected == 0 ? 8 : 164;
          M5.Display.fillRect(x, cardBottom + 3, static_cast<int>(148 * (now - g.holdSince) / holdFor), 5, honey);
        }
      }
    }
  } else if (g.stage == Stage::Feedback) {
    // Level the compact, then tilt either way (or tap) for the next card.
    bool tiltNext = false;
    if (g.tilt && sample(now)) {
      float tilt = (g.latestX-g.centerX)*g.axisX + (g.latestY-g.centerY)*g.axisY;
      g.projection = 0.55f*g.projection + 0.45f*tilt;
      // The answer stays up until a new flick: the compact must first rest level for a
      // moment (so the flick that answered cannot carry over), then be tipped firmly.
      if (!g.answerShownAt) g.answerShownAt = now;
      const bool level = std::fabs(tilt) < 0.08f && std::fabs(g.projection) < 0.08f;
      if (!level) g.levelSince = 0;
      else if (!g.levelSince) g.levelSince = now;
      if (g.levelSince && now - g.levelSince >= 450 && now - g.answerShownAt >= 900) g.nextArmed = true;
      tiltNext = g.nextArmed && std::fabs(g.projection) >= 0.22f;
    }
    if (!(primary || press || tiltNext)) { tellWand(now); return false; }
    if (g.round+1 == rounds) g.stage = Stage::Score;
    else { ++g.round; roundStart(); }
    paint();
  } else if (g.stage == Stage::Score && (primary || press)) {
    if (!press && x < 154) return true;
    if (press || x >= 163) {
      g.round = 0; g.score = 0;
      roundStart();
      paint();
    }
  }
  tellWand(now, g.stage != before || g.selected != selectedBefore);
  return false;
}
// Leaving the arcade: the wand shows that the game is closed.
inline void leave() {
  if (!radio::isPaired()) return;
  wand::State s = {};
  wand::stampHeader(s, wand::kState);
  s.stage = wand::kAway;
  radio::send(s);
}
}  // namespace arcade
