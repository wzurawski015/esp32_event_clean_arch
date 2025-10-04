#!/usr/bin/env bash
set -Eeuo pipefail
#/*! \file scripts/flash-monitor.sh
# *  \brief Jednoprzyciskowy Flash + Monitor z filtrowaniem logów IDF.
# *
# *  \details
# *   - Flashuje firmware i odpala `idf.py monitor` z domyślnym filtrem logów.
# *   - Filtrowanie można nadpisać zmienną środowiskową \c IDF_MONITOR_FILTER.
# *   - Dodatkowe argumenty po \c -- są przekazywane prosto do monitora.
# *
# *  \par Szybki start
# *  \code
# *  ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
# *  # lub z własnym filtrem:
# *  IDF_MONITOR_FILTER="*:I,APP:W" ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
# *  # lub jednorazowo przez argumenty po "--":
# *  ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh -- --print_filter "*:I,APP:W"
# *  \endcode
# *
# *  \dot
# *  digraph Tooling {
# *    rankdir=LR; node [shape=box, fontsize=10];
# *    Host -> "flash-monitor.sh" -> "idf.sh" -> "idf.py flash";
# *    "flash-monitor.sh" -> "idf.sh (monitor)" -> "idf.py monitor" [label="--print_filter"];
# *    "idf.py monitor" -> "ESP32-Cx UART";
# *  }
# *  \enddot
# */

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PROJ="${PROJ:-demo_lcd_rgb}"
TARGET="${TARGET:-esp32c6}"

ESPPORT="${ESPPORT:-$("${ROOT}/scripts/find-port.sh" || true)}"
if [[ -z "${ESPPORT}" ]]; then
  echo "ERR: Nie znaleziono portu. Ustaw ESPPORT=/dev/ttyUSBx lub /dev/ttyACMx" >&2
  exit 2
fi

ESPBAUD="${ESPBAUD:-460800}"
MONBAUD="${MONBAUD:-115200}"

# Domyślny filtr IDF monitor:
#  - wszystko na INFO,
#  - tag "APP" (ticki) dopiero od WARN,
#  - "LOGCLI" zostaw INFO (widzimy komunikaty CLI/REPL).
IDF_MONITOR_FILTER_DEFAULT="*:I,APP:W,LOGCLI:I"
FILTER_OPT=(--print_filter "${IDF_MONITOR_FILTER:-$IDF_MONITOR_FILTER_DEFAULT}")

echo "Flash+Monitor: ${ESPPORT}  (Ctrl+] aby wyjść)  |  IMAGE=${IDF_IMAGE:-esp32-idf:5.5.1}"

# Flash
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${ESPBAUD}" flash

# Monitor (dodatkowe argumenty po '--' trafią do idf.py monitor)
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" monitor --monitor-baud "${MONBAUD}" "${FILTER_OPT[@]}" "$@"
