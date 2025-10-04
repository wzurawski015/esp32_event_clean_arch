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

# Katalogi projektu
PROJ_DIR="${ROOT}/firmware/projects/${PROJ}"
BUILD_DIR="${PROJ_DIR}/build"

# Projekt sam warstwuje defaults w CMake – nie nadpisujemy tego zmienną środowiska
unset SDKCONFIG_DEFAULTS

# --- Opcjonalny filtr monitora (TAG:LVL[,TAG2:LVL2...])
RAW_FILTER="${IDF_MONITOR_FILTER:-}"
RAW_FILTER="${RAW_FILTER//,/ }"
RAW_FILTER="${RAW_FILTER//;/ }"
read -r -a _TOKENS <<< "$RAW_FILTER"
MON_FILTER_ARGS=()
for tok in "${_TOKENS[@]}"; do
  [[ -z "$tok" ]] && continue
  if [[ "$tok" != *:* || "$(tr -dc ':' <<<"$tok" | wc -c)" -ne 1 ]]; then
    echo "ERR: Niepoprawny token filtra: '$tok' (format TAG:LVL, np. '*:I' 'APP:W' 'DFR_LCD:D')" >&2
    exit 3
  fi
  MON_FILTER_ARGS+=( --print_filter "$tok" )
done

echo "Flash+Monitor: ${ESPPORT}  (Ctrl+] aby wyjść)"
echo "Monitor filter: ${RAW_FILTER:-(brak – wyświetlamy wszystko)}"

# --- Na żądanie: twardy reset konfiguracji (usuń stary sdkconfig i build)
if [[ "${RESET_SDKCONFIG:-0}" == "1" ]]; then
  rm -f  "${PROJ_DIR}/sdkconfig" || true
  rm -rf "${BUILD_DIR}" || true
fi

# --- ensure_target: uruchom 'set-target' tylko gdy potrzebne
ensure_target() {
  local cfg="${PROJ_DIR}/sdkconfig"
  local cur=""
  if [[ -f "${cfg}" ]]; then
    cur="$(grep -E '^CONFIG_IDF_TARGET="' "${cfg}" | cut -d'"' -f2 || true)"
  fi

  if [[ -z "${cur}" || "${cur}" != "${TARGET}" ]]; then
    echo "set-target: want='${TARGET}' cur='${cur:-<none>}' → wykonuję idf.py set-target"
    PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
      "${ROOT}/scripts/idf.sh" set-target "${TARGET}"
  else
    echo "set-target: OK (cur='${cur}') — pomijam"
  fi
}

# --- Krok 1: upewnij się, że target jest właściwy (bez zbędnych fullclean/rotacji sdkconfig)
ensure_target

# --- Krok 2: build (wygeneruje build/flash_args)
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" build

# --- Krok 3: pre-check flash_size → porównaj defaults vs to, co trafiło do flash_args
WANT="$(grep -E '^CONFIG_ESPTOOLPY_FLASHSIZE="' "${PROJ_DIR}/sdkconfig.${TARGET}.defaults" | cut -d'"' -f2 || true)"
ARGS_FILE="${BUILD_DIR}/flash_args"
GOT=""
if [[ -f "${ARGS_FILE}" ]]; then
  # złap wartość po --flash_size (np. '8MB' albo 'detect')
  GOT="$(grep -oE -- '--flash_size[[:space:]]+[^[:space:]]+' "${ARGS_FILE}" | awk '{print $2}' | head -n1 || true)"
fi
echo "Pre-check flash_size: want='${WANT}' got='${GOT}'"

# Akceptujemy: (1) dokładny rozmiar (np. 8MB) lub (2) 'detect' (esptool poprawi nagłówek w locie)
if [[ -n "${WANT}" ]]; then
  if [[ "${GOT}" != "${WANT}" && "${GOT}" != "detect" ]]; then
    echo "ERR: Niezgodność flash_size: build użyje '${GOT}', a defaults mówią '${WANT}'." >&2
    echo "    Uruchom ponownie z RESET_SDKCONFIG=1 lub usuń ręcznie:" >&2
    echo "    ${PROJ_DIR}/sdkconfig oraz ${BUILD_DIR}" >&2
    exit 6
  fi
fi

# --- Krok 4: flash
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${ESPBAUD}" flash

# --- Krok 5: monitor
PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" \
  "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" monitor \
    --monitor-baud "${MONBAUD}" \
    "${MON_FILTER_ARGS[@]}" \
    "$@"
