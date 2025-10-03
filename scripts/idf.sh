#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW="${ROOT}/firmware"
PROJ="${PROJ:-demo_lcd_rgb}"
TARGET="${TARGET:-esp32c6}"

if [[ ! -d "${FW}/projects/${PROJ}" ]]; then
  echo "ERR: projekt '${PROJ}' nie istnieje w firmware/projects" >&2
  exit 2
fi

# Obraz Dockera: lokalny (np. esp32-idf:5.5.1) lub oficjalny (espressif/idf:v5.5.1)
IMAGE="${IDF_IMAGE:-esp32-idf:5.5.1}"

# Podłącz port - tylko jako urządzenie; -p przekażemy jawnie do idf.py
PORT_DEV=()
if [[ -n "${ESPPORT:-}" ]]; then
  PORT_DEV=(--device="${ESPPORT}:${ESPPORT}")
fi

# -it tylko jeśli mamy TTY (np. w CI brak TTY)
TTY=()
if [[ -t 1 ]]; then
  TTY=(-it)
fi

docker run --rm "${TTY[@]}" \
  -e TERM="${TERM:-xterm-256color}" \
  -e COLUMNS="${COLUMNS:-120}" \
  -e LINES="${LINES:-40}" \
  -e IDF_CCACHE_ENABLE=1 \
  -e IDF_TARGET="${TARGET}" \
  -e PROJ="${PROJ}" \
  -e TARGET="${TARGET}" \
  -v esp-idf-espressif:/root/.espressif \
  -v esp-idf-ccache:/root/.cache/ccache \
  -v "${FW}:/fw" \
  "${PORT_DEV[@]}" \
  "${IMAGE}" \
  bash -lc '
    set -Eeuo pipefail
    . "${IDF_PATH}/export.sh"
    cd "/fw/projects/${PROJ}"
    # ustaw target tylko jeśli różni się od oczekiwanego
    CUR=$(grep -E "^CONFIG_IDF_TARGET=\"" sdkconfig 2>/dev/null | sed -E "s/.*\"(.+)\".*/\1/" || true)
    if [[ "${CUR}" != "${TARGET}" ]]; then
      idf.py set-target "${TARGET}"
    fi
    # wykonaj dokładnie to, co zostało przekazane do idf.sh
    idf.py "$@"
  ' -- "$@"
