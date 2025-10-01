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

# Podłącz port - tylko jako urządzenie, argumenty -p przekażemy jawnie w poleceniach wywołujących
PORT_DEV=""
if [[ -n "${ESPPORT:-}" ]]; then
  PORT_DEV="--device=${ESPPORT}:${ESPPORT}"
fi

docker run --rm -it \
  -e TERM=xterm-256color \
  -e IDF_CCACHE_ENABLE=1 \
  -e IDF_TARGET="${TARGET}" \
  -v esp-idf-espressif:/root/.espressif \
  -v esp-idf-ccache:/root/.cache/ccache \
  -v "${FW}:/fw" \
  ${PORT_DEV} \
  esp32-idf:5.3-docs bash -lc "
    set -Eeuo pipefail
    cd /fw/projects/${PROJ}
    # ustaw target tylko jeśli różni się od oczekiwanego
    CUR=\$(grep -E '^CONFIG_IDF_TARGET=\"' sdkconfig 2>/dev/null | sed -E 's/.*\"(.*)\".*/\1/' || true)
    if [[ \"\${CUR}\" != \"${TARGET}\" ]]; then
      idf.py set-target ${TARGET}
    fi
    # wykonaj dokładnie to, co zostało przekazane do idf.sh
    idf.py $*
"
