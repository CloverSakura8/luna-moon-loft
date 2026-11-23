#!/bin/zsh
set -eu
cd "${0:A:h}"
export PLATFORMIO_CORE_DIR="$PWD/.platformio"
if command -v pio >/dev/null 2>&1; then
  pio run -e cores3 -e pocket
else
  if [[ ! -x .buildenv/bin/pio ]]; then
    # The system Python on Apple Silicon is also supported by PlatformIO 6.
    if [[ -x /usr/bin/python3 ]]; then py=/usr/bin/python3; else py=python3; fi
    "$py" -m venv .buildenv
    .buildenv/bin/python -m pip install 'platformio>=6.1,<7'
  fi
  .buildenv/bin/pio run -e cores3 -e pocket
fi
echo "Compiled .pio/build/cores3/firmware.bin (CoreS3) and .pio/build/pocket/firmware.bin (StickS3 Pocket Luna)."
echo "Build only. Both boards still have their existing firmware."
