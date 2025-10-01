#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PROJ="${PROJ:-demo_lcd_rgb}"
TARGET="${TARGET:-esp32c6}"
ESPPORT="${ESPPORT:-$("${ROOT}/scripts/find-port.sh" || true)}"
if [[ -z "${ESPPORT}" ]]; then
  echo "ERR: Nie znaleziono portu. Ustaw ESPPORT=/dev/ttyUSBx lub /dev/ttyACMx" >&2
  exit 2
fi

MONBAUD="${MONBAUD:-115200}"

PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" monitor --monitor-baud "${MONBAUD}"
