#!/usr/bin/env bash
#==============================================================================
# @file flash-monitor.sh
# @brief Flash + Monitor dla projektu ESP-IDF z wygodnym filtrowaniem logów.
#
# @details
#  - Buduje i flashuje wybrany projekt, następnie uruchamia idf_monitor.
#  - Wspiera przyjazne filtrowanie logów przez zmienną IDF_MONITOR_FILTER.
#    Składnia wejściowa (elastyczna):
#       IDF_MONITOR_FILTER="*:I APP:W LOGCLI:I"
#       IDF_MONITOR_FILTER="*:I,APP:W,LOGCLI:I"
#       IDF_MONITOR_FILTER="*:I;APP:W;LOGCLI:I"
#    Każdy token musi być postaci TAG:LVL, gdzie LVL ∈ {E,W,I,D,V}.
#    Skrypt zamienia to na wielokrotne --print_filter 'TAG:LVL'
#    wymagane przez ESP-IDF 5.5.
#
# @dot
# digraph MON {
#   rankdir=LR; node [shape=box, fontsize=10];
#   subgraph cluster_host { label="Host"; "flash-monitor.sh"; }
#   "flash-monitor.sh" -> "idf.sh flash";
#   "flash-monitor.sh" -> "Filtr tokens" [label="IDF_MONITOR_FILTER"];
#   "Filtr tokens" -> "idf.sh monitor" [label="--print_filter 'TAG:LVL' x N"];
# }
# @enddot
#
# @note Skróty w idf_monitor:
#   - Ctrl+T,Y  — pauza/wznowienie wyjścia,
#   - Ctrl+]     — wyjście,
#   - Ctrl+T,H   — pomoc.
#
#==============================================================================

set -Eeuo pipefail

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

#--------------------------- Filtr logów (IDF 5.5) ----------------------------
# Domyślnie: global INFO, APP od WARN (wycisza ticki INFO), LOGCLI na INFO.
IDF_MONITOR_FILTER_DEFAULT="*:I APP:W LOGCLI:I"
RAW_FILTER="${IDF_MONITOR_FILTER:-$IDF_MONITOR_FILTER_DEFAULT}"

# Pozwól używać spacji / przecinków / średników jako separatorów.
RAW_FILTER="${RAW_FILTER//,/ }"
RAW_FILTER="${RAW_FILTER//;/ }"

# Zbuduj listę wielokrotnych --print_filter 'TAG:LVL'
read -r -a _TOKENS <<< "$RAW_FILTER"
MON_FILTER_ARGS=()
for tok in "${_TOKENS[@]}"; do
  [[ -z "$tok" ]] && continue
  # Walidacja bazowa: dokładnie jeden dwukropek
  if [[ "$tok" != *:* || "$(tr -dc ':' <<<"$tok" | wc -c)" -ne 1 ]]; then
    echo "ERR: Niepoprawny token filtra: '$tok' (wymagany format TAG:LVL)" >&2
    echo "    Przykłady: '*:I'  'APP:W'  'LOGCLI:I'  'DFR_LCD:D'" >&2
    exit 3
  fi
  # Dodaj jeden argument --print_filter na token (tak wymaga IDF 5.5)
  MON_FILTER_ARGS+=( --print_filter "$tok" )
done

echo "Flash+Monitor: ${ESPPORT}  (Ctrl+] aby wyjść)  |  IMAGE=${IDF_IMAGE:-esp32-idf:5.5.1}"
echo "Monitor filter: ${RAW_FILTER}"

#--------------------------------- Flash --------------------------------------
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${ESPBAUD}" flash

#-------------------------------- Monitor -------------------------------------
# Przekazujemy przygotowane filtry jako oddzielne argumenty:
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" monitor \
    --monitor-baud "${MONBAUD}" \
    "${MON_FILTER_ARGS[@]}" \
    "$@"
