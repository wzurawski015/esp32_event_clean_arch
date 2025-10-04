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

ESPBAUD="${ESPBAUD:-460800}"
MONBAUD="${MONBAUD:-115200}"

# --- NIE nadpisujemy warstw defaults środowiskiem (projekt definiuje je sam)
unset SDKCONFIG_DEFAULTS

# Filtr monitor (TAG:LVL), opcjonalny
RAW_FILTER="${IDF_MONITOR_FILTER:-}"
RAW_FILTER="${RAW_FILTER//,/ }"
RAW_FILTER="${RAW_FILTER//;/ }"
read -r -a _TOKENS <<< "$RAW_FILTER"
MON_FILTER_ARGS=()
for tok in "${_TOKENS[@]}"; do
  [[ -z "$tok" ]] && continue
  if [[ "$tok" != *:* || "$(tr -dc ':' <<<"$tok" | wc -c)" -ne 1 ]]; then
    echo "ERR: Niepoprawny token filtra: '$tok' (format TAG:LVL)" >&2
    echo "    Przykłady: '*:I'  'APP:W'  'LOGCLI:I'  'DFR_LCD:D'" >&2
    exit 3
  fi
  MON_FILTER_ARGS+=( --print_filter "$tok" )
done

esptool_cmd() {
  if [[ -x /opt/esp/python_env/idf5.5_py3.12_env/bin/python ]]; then
    /opt/esp/python_env/idf5.5_py3.12_env/bin/python -m esptool "$@"
  else
    python3 -m esptool "$@"
  fi
}

check_flash_hdr() {
  local proj_dir="${ROOT}/firmware/projects/${PROJ}"
  local appbin="${proj_dir}/build/${PROJ}.bin"
  local bootbin="${proj_dir}/build/bootloader/bootloader.bin"
  local want="$(grep -E '^CONFIG_ESPTOOLPY_FLASHSIZE="' "${proj_dir}/sdkconfig.${TARGET}.defaults" | cut -d'"' -f2 || true)"

  [[ -z "${want}" ]] && return 0
  [[ ! -f "${appbin}" || ! -f "${bootbin}" ]] && return 0

  echo "Pre-check nagłówków: oczekiwany Flash size = ${want}"
  if ! esptool_cmd image_info "${appbin}" | grep -q "Flash size: ${want}"; then
    echo "ERR: App image header ma zły Flash size (nie ${want})." >&2
    esptool_cmd image_info "${appbin}" || true
    exit 6
  fi
  if ! esptool_cmd image_info "${bootbin}" | grep -q "Flash size: ${want}"; then
    echo "ERR: Bootloader image header ma zły Flash size (nie ${want})." >&2
    esptool_cmd image_info "${bootbin}" || true
    exit 7
  fi
}

echo "Flash+Monitor: ${ESPPORT}  (Ctrl+] aby wyjść)"
echo "Monitor filter: ${RAW_FILTER:-(brak – wyświetlamy wszystko)}"

# BUILD (musi powstać .bin z nagłówkami)
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" build

check_flash_hdr

# FLASH
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${ESPBAUD}" flash

# MONITOR
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" monitor \
    --monitor-baud "${MONBAUD}" \
    "${MON_FILTER_ARGS[@]}" \
    "$@"
