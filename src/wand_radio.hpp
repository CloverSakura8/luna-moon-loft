#pragma once
// ESP-NOW radio shared by both boards. The wand broadcasts until a compact answers,
// then each side talks only to the other's address.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "wand_link.hpp"

namespace radio {
namespace detail {
inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
inline uint8_t inbox[32];
inline int inboxLength = 0;
inline uint8_t inboxFrom[6];
inline uint32_t inboxAt = 0;
inline bool fresh = false;
inline bool started = false;
inline uint8_t partner[6];
inline bool paired = false;
inline uint8_t wanted = 0;   // packet type this board listens for
constexpr uint8_t everyone[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

inline void received(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (length < 6 || length > static_cast<int>(sizeof(inbox)) || memcmp(data, wand::kMagic, 4) != 0 ||
      data[4] != wand::kVersion || data[5] != wanted) return;
  portENTER_CRITICAL(&lock);
  // Once paired, ignore every other sender.
  if (!paired || memcmp(info->src_addr, partner, 6) == 0) {
    memcpy(inbox, data, length);
    inboxLength = length;
    memcpy(inboxFrom, info->src_addr, 6);
    inboxAt = millis();
    fresh = true;
  }
  portEXIT_CRITICAL(&lock);
}
inline bool addPeer(const uint8_t* address) {
  if (esp_now_is_peer_exist(address)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, address, 6);
  peer.channel = wand::kChannel;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}
}  // namespace detail

inline bool begin(uint8_t listenFor) {
  using namespace detail;
  if (started) return true;
  wanted = listenFor;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(wand::kChannel, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(received);
  addPeer(everyone);
  started = true;
  return true;
}
inline bool isPaired() { return detail::paired; }
inline void unpair() {
  portENTER_CRITICAL(&detail::lock);
  detail::paired = false;
  portEXIT_CRITICAL(&detail::lock);
}
// Milliseconds since the partner was last heard.
inline uint32_t silence() { return millis() - detail::inboxAt; }

// Take the newest packet. The first valid sender becomes the partner.
template <typename T> inline bool take(uint8_t type, T& out) {
  using namespace detail;
  uint8_t copy[sizeof(inbox)], from[6];
  int length;
  portENTER_CRITICAL(&lock);
  const bool had = fresh;
  fresh = false;
  length = inboxLength;
  memcpy(copy, inbox, sizeof(copy));
  memcpy(from, inboxFrom, 6);
  portEXIT_CRITICAL(&lock);
  if (!had || !wand::parse(copy, length, type, out)) return false;
  if (!paired) {
    portENTER_CRITICAL(&lock);
    memcpy(partner, from, 6);
    paired = true;
    portEXIT_CRITICAL(&lock);
    addPeer(partner);
  }
  return true;
}
template <typename T> inline void send(const T& packet) {
  using namespace detail;
  if (!started) return;
  esp_now_send(paired ? partner : everyone, reinterpret_cast<const uint8_t*>(&packet), sizeof(T));
}
}  // namespace radio
