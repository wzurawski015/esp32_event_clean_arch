#!/usr/bin/env bash
#==============================================================================
# @file flash-monitor.sh
# @brief Flash + Monitor dla projektu ESP-IDF z filtrem logów (opcjonalnym).
#
# Użycie:
#   TARGET=esp32c6 CONSOLE=uart RESET_SDKCONFIG=1 \
#     IDF_MONITOR_FILTER="*" ESPPORT=$(./scripts/find-port.sh) ./scripts/flash-monitor.sh
#
# - IDF_MONITOR_FILTER: np. "*:I APP:W LOGCLI:I" albo puste (pokaż wszystko).
# - CONSOLE=usb|uart   : dołącza sdkconfig.console.<console>.defaults
# - TARGET=...         : jeśli istnieje sdkconfig.<target>.defaults, to zostanie dołączony
# - RESET_SDKCONFIG=1  : usunie wygenerowany sdkconfig (regeneracja z defaults)
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

PROJDIR="${ROOT}/firmware/projects/${PROJ}"

#---------------------- Warstwa defaults dla sdkconfig ------------------------
CFG_LIST=()

# Bazowy defaults (z repo)
if [[ -f "${PROJDIR}/sdkconfig.defaults" ]]; then
  CFG_LIST+=("${PROJDIR}/sdkconfig.defaults")
fi

# Wariant per-console: sdkconfig.console.usb.defaults / sdkconfig.console.uart.defaults
if [[ "${CONSOLE:-}" == "usb" && -f "${PROJDIR}/sdkconfig.console.usb.defaults" ]]; then
  CFG_LIST+=("${PROJDIR}/sdkconfig.console.usb.defaults")
elif [[ "${CONSOLE:-}" == "uart" && -f "${PROJDIR}/sdkconfig.console.uart.defaults" ]]; then
  CFG_LIST+=("${PROJDIR}/sdkconfig.console.uart.defaults")
fi

# (Opcjonalnie) wariant per-target: sdkconfig.<target>.defaults
if [[ -f "${PROJDIR}/sdkconfig.${TARGET}.defaults" ]]; then
  CFG_LIST+=("${PROJDIR}/sdkconfig.${TARGET}.defaults")
fi

if (( ${#CFG_LIST[@]} > 0 )); then
  export SDKCONFIG_DEFAULTS="$(IFS=';'; echo "${CFG_LIST[*]}")"
  echo "SDKCONFIG_DEFAULTS = ${SDKCONFIG_DEFAULTS}"
fi

# (Opcjonalnie) wymuś pełną regenerację configu
if [[ "${RESET_SDKCONFIG:-0}" == "1" ]]; then
  rm -f "${PROJDIR}/sdkconfig"
  echo "Usunięto ${PROJDIR}/sdkconfig (RESET_SDKCONFIG=1)"
fi

#--------------------------- Filtr logów (opcjonalny) -------------------------
RAW_FILTER="${IDF_MONITOR_FILTER:-}"  # BRAK domyślnego filtra

RAW_FILTER="${RAW_FILTER//,/ }"
RAW_FILTER="${RAW_FILTER//;/ }"

MON_FILTER_ARGS=()
if [[ -n "${RAW_FILTER// }" ]]; then
  read -r -a _TOKENS <<< "$RAW_FILTER"
  for tok in "${_TOKENS[@]}"; do
    [[ -z "$tok" ]] && continue
    if [[ "$tok" != *:* || "$(tr -dc ':' <<<"$tok" | wc -c)" -ne 1 ]]; then
      echo "ERR: Niepoprawny token filtra: '$tok' (format TAG:LVL)" >&2
      echo "    Przykłady: '*:I'  'APP:W'  'LOGCLI:I'  'DFR_LCD:D'" >&2
      exit 3
    fi
    MON_FILTER_ARGS+=( --print_filter "$tok" )
  done
fi

echo "Flash+Monitor: ${ESPPORT}  (Ctrl+] aby wyjść)"
if (( ${#MON_FILTER_ARGS[@]} == 0 )); then
  echo "Monitor filter: (brak – wyświetlamy wszystko)"
else
  echo "Monitor filter: ${RAW_FILTER}"
fi

#--------------------------------- Flash --------------------------------------
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${ESPBAUD}" flash

#-------------------------------- Monitor -------------------------------------
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" monitor \
    --monitor-baud "${MONBAUD}" \
    "${MON_FILTER_ARGS[@]}" \
    "$@"
