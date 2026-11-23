# Voice on the CoreS3

These are my changes to the M5Stack CoreS3 example of a third-party, Apache-2.0 licensed live-voice SDK for ESP32-S3 boards. They are not a complete project: to build them you need that SDK, its toolchain (ESP-IDF 5.5) and an account with a realtime voice provider. The upstream licence is in `LICENSE-upstream`.

What I changed:

- `partitions.csv` matches the main firmware's layout, so the voice program can sit in the second app slot (offset `0x650000`) beside it.
- `main/main.cpp` points the next boot back at the main firmware as soon as voice starts, sets Luna up as an easy-going Japanese conversation partner, and returns home after a chat ends or when the device sits idle.
- `main/View.cpp` adds a "Loft" button that ends the chat and returns home, and replaces live captions with a simple status line, because the example's font has no Japanese glyphs.
- `main/Board.hpp` and `main/CMakeLists.txt` carry the small declarations those changes need.

Wi-Fi details and the API key are supplied at build time from private files outside the repository. Never commit them.
