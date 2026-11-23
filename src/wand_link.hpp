#pragma once
// Shared by the StickS3 wand (K150) and the CoreS3 compact (K128).
// Pure protocol and motion logic: no hardware calls, so it is unit-tested on the Mac
// (test/wand_logic_test.cpp).
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wand {

constexpr uint8_t kMagic[4] = {'L', 'U', 'N', 'W'};
constexpr uint8_t kVersion = 1;
constexpr uint8_t kChannel = 1;
enum : uint8_t { kMotion = 1, kState = 2, kFeed = 3, kPoke = 4 };
// What the compact is showing, so the wand can prompt the player.
enum : uint8_t { kSetup = 0, kQuestion = 1, kFeedback = 2, kScore = 3, kAway = 255 };

#pragma pack(push, 1)
struct Motion {            // wand -> compact, about 20 times a second
  uint8_t magic[4];
  uint8_t version, type;
  uint16_t seq;
  int8_t side;             // -1 left, 0 neutral, +1 right
  int8_t degrees;          // tilt from the player's neutral, right positive
  uint8_t presses;         // counts front-button presses; wraps
  uint8_t calibrated;
};
struct State {             // compact -> wand, a few times a second
  uint8_t magic[4];
  uint8_t version, type;
  uint8_t stage;
  int8_t selected;         // -1 none, 0 left answer, 1 right answer
  uint8_t correct;         // meaningful in kFeedback
  uint8_t stamps, round, rounds;
};
struct Feed {              // compact -> Pocket Luna, every couple of seconds
  uint8_t magic[4];
  uint8_t version, type;
  uint32_t total;          // running total of everything Tanya has earned on the compact
  uint16_t event;          // changes each time something happens
  uint8_t kind;            // 1 kanji, 2 task, 3 quiz right, 4 photo, 5 quote, 6 sticker, 7 a miss
};
struct Poke {              // Pocket Luna -> compact: the stick's button was double-pressed
  uint8_t magic[4];
  uint8_t version, type;
  uint16_t seq;
};
#pragma pack(pop)

template <typename T> inline void stampHeader(T& packet, uint8_t type) {
  std::memcpy(packet.magic, kMagic, 4);
  packet.version = kVersion;
  packet.type = type;
}
template <typename T> inline bool parse(const uint8_t* data, int length, uint8_t type, T& out) {
  if (length != static_cast<int>(sizeof(T)) || std::memcmp(data, kMagic, 4) != 0) return false;
  std::memcpy(&out, data, sizeof(T));
  return out.version == kVersion && out.type == type;
}

// Wrist tilt -> left / neutral / right. The player sets a neutral pose; a dead zone
// around it keeps a resting hand from choosing anything, and hysteresis stops flicker.
struct Tilt {
  static constexpr float kChoose = 16.0f;   // degrees from neutral to pick a side
  static constexpr float kRelease = 8.0f;   // back inside this is neutral again
  // On the StickS3 held with its screen facing the player, lowering the right edge
  // makes the X reading fall. Verified on Tanya's wand; flip here if a board differs.
  static constexpr float kRightSign = -1.0f;

  bool calibrated = false;
  float neutral = 0, smooth = 0;
  int side = 0;

  static float angle(float ax, float ay, float az) {
    return std::atan2(ax, std::sqrt(ay * ay + az * az)) * 57.29578f;
  }
  void calibrate(float ax, float ay, float az) {
    neutral = angle(ax, ay, az);
    smooth = 0;
    side = 0;
    calibrated = true;
  }
  // Returns degrees from neutral (right positive) and updates `side`.
  float update(float ax, float ay, float az) {
    if (!calibrated) return 0;
    const float now = (angle(ax, ay, az) - neutral) * kRightSign;
    smooth = 0.6f * smooth + 0.4f * now;
    if (side == 0) {
      if (smooth >= kChoose) side = 1;
      else if (smooth <= -kChoose) side = -1;
    } else if (std::fabs(smooth) < kRelease) {
      side = 0;
    } else if (side == 1 && smooth <= -kChoose) {
      side = -1;
    } else if (side == -1 && smooth >= kChoose) {
      side = 1;
    }
    return smooth;
  }
};

// The compact's view of the wand during a question. One button press is one
// confirmation, however many packets repeat it or go missing.
struct Chooser {
  bool armed = false;      // the wand must rest at neutral before a new question listens
  bool known = false;
  uint8_t presses = 0;
  int selected = -1;

  void newQuestion() { armed = false; selected = -1; }
  // Returns true when the highlighted answer changed.
  bool motion(const Motion& m) {
    if (!m.calibrated) return false;
    if (m.side == 0) { armed = true; return false; }
    if (!armed) return false;
    const int want = m.side < 0 ? 0 : 1;
    if (want == selected) return false;
    selected = want;
    return true;
  }
  // Returns true exactly once per physical press.
  bool pressed(const Motion& m) {
    if (!known) { known = true; presses = m.presses; return false; }
    if (m.presses == presses) return false;
    presses = m.presses;
    return true;
  }
};

}  // namespace wand
