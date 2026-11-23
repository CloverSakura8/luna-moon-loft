#!/bin/zsh
set -eu
cd "${0:A:h}"
# Load provider settings if you have made a .env (see .env.example).
if [[ -f .env ]]; then set -a; source ./.env; set +a; fi
exec python3 companion.py
