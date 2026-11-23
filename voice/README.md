# Voice on the CoreS3

These are my changes to the M5Stack example of [openai/openai-live-esp32](https://github.com/openai/openai-live-esp32), an experimental, Apache-2.0 licensed live-voice SDK for ESP32-S3 boards. They were made against commit [`6520b96`](https://github.com/openai/openai-live-esp32/tree/6520b96840493c285ecbb7c72cace3b8116b619d). The upstream licence is in `LICENSE-upstream`.

They are not a complete project. To build the voice program you need that SDK, ESP-IDF 5.5.2 and an account with access to the provider's live voice API.

## What I changed

- `partitions.csv` matches the main firmware's layout, so the voice program can sit in the second app slot (offset `0x650000`) beside it.
- `main/main.cpp` points the next boot back at the main firmware as soon as voice starts, sets Luna up as an easy-going Japanese conversation partner, and returns home after a chat ends or when the device sits idle.
- `main/View.cpp` adds a "Loft" button that ends the chat and returns home, and replaces live captions with a simple status line, because the example's font has no Japanese glyphs.
- `main/Board.hpp` and `main/CMakeLists.txt` carry the small declarations those changes need.

## Building it

1. Check out the SDK at the commit above and follow its `examples/m5stack/README.md` to install ESP-IDF 5.5.2 and prepare your private Wi-Fi and key files.
2. Copy this folder's `partitions.csv` over `examples/m5stack/partitions.csv`, and the files in `main/` over `examples/m5stack/main/`.
3. Build with `idf.py -C examples/m5stack build`.
4. Install the main firmware first (`pio run -e cores3 -t upload` from the repository root), then write only the voice app into the second slot:

   ```sh
   esptool --chip esp32s3 --port YOUR_PORT write-flash 0x650000 build/live_m5stack.bin
   ```

   Do not use `idf.py flash` here: it would replace the bootloader and the main firmware.

The preparation script needs Python 3.12 or newer; the rest of the toolchain is happy with older versions. ESP-IDF cannot build from a folder whose path contains spaces.

Wi-Fi details and the API key are supplied at build time from private files outside the repository. Never commit them. If your phone hotspot's name contains an apostrophe, save it with both the straight and the curly form: phones often broadcast the curly one.
