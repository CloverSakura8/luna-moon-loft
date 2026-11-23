// Host test for the wand's pure logic: c++ -std=c++17 -Isrc test/wand_logic_test.cpp -o /tmp/wand_test
#include <cassert>
#include <cstdio>
#include "wand_link.hpp"
using namespace wand;

// Gravity reading for a wand rolled `deg` to the player's right from flat.
static void rolled(float deg, float& ax, float& ay, float& az) {
  const float r = deg / 57.29578f * Tilt::kRightSign;
  ax = std::sin(r); ay = 0; az = std::cos(r);
}
static Motion feed(Tilt& t, float deg, uint8_t presses, int repeats = 12) {
  float ax, ay, az;
  Motion m = {};
  stampHeader(m, kMotion);
  for (int i = 0; i < repeats; ++i) { rolled(deg, ax, ay, az); t.update(ax, ay, az); }
  m.side = static_cast<int8_t>(t.side); m.presses = presses; m.calibrated = t.calibrated;
  return m;
}

int main() {
  // Neutral can be any comfortable pose, here 25 degrees off level.
  Tilt t; float ax, ay, az;
  assert(feed(t, 40, 0).side == 0);                 // nothing before calibration
  rolled(25, ax, ay, az); t.calibrate(ax, ay, az);
  assert(feed(t, 25, 0).side == 0);
  assert(feed(t, 25 + 6, 0).side == 0);             // inside the dead zone
  assert(feed(t, 25 + 22, 0).side == 1);            // right
  assert(feed(t, 25 + 11, 0).side == 1);            // hysteresis holds until near neutral
  assert(feed(t, 25 + 3, 0).side == 0);
  assert(feed(t, 25 - 22, 0).side == -1);           // left
  assert(feed(t, 25 + 22, 0).side == 1);            // straight across changes sides

  // The acceptance round: preview left, change to right, press once.
  Tilt w; rolled(0, ax, ay, az); w.calibrate(ax, ay, az);
  Chooser c;
  Motion m = feed(w, 0, 7);
  assert(!c.pressed(m));                            // first packet only sets the baseline
  c.newQuestion();
  m = feed(w, 30, 7);  assert(!c.motion(m) && c.selected == -1);   // tilted before resting: ignored
  m = feed(w, 0, 7);   assert(!c.motion(m) && c.armed);
  m = feed(w, -30, 7); assert(c.motion(m) && c.selected == 0);     // left answer previewed
  m = feed(w, 0, 7);   assert(!c.motion(m) && c.selected == 0);    // resting keeps the choice
  m = feed(w, 30, 7);  assert(c.motion(m) && c.selected == 1);     // changed her mind
  assert(!c.pressed(m));
  m.presses = 8;       assert(c.pressed(m));                       // one press ...
  assert(!c.pressed(m)); assert(!c.pressed(m));                    // ... repeated packets count once
  c.newQuestion();     assert(c.selected == -1);
  m = feed(w, 30, 8);  assert(!c.motion(m) && c.selected == -1);   // still tilted: next card starts empty
  m.presses = 9;       assert(c.pressed(m));
  m.presses = 0;       assert(c.pressed(m));                       // counter wrap is still one press

  // Packets from strangers or other versions are rejected.
  Motion good = {}; stampHeader(good, kMotion); Motion out;
  assert(parse(reinterpret_cast<uint8_t*>(&good), sizeof(good), kMotion, out));
  assert(!parse(reinterpret_cast<uint8_t*>(&good), sizeof(good), kState, out));
  assert(!parse(reinterpret_cast<uint8_t*>(&good), sizeof(good) - 1, kMotion, out));
  good.magic[0] = 'X';
  assert(!parse(reinterpret_cast<uint8_t*>(&good), sizeof(good), kMotion, out));
  std::puts("wand logic: all checks passed");
}
