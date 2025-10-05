#!/usr/bin/env bash
set -Eeuo pipefail

# ===== Repo root ==============================================================
if ROOT_GIT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null)"; then
  ROOT="${ROOT_GIT}"
else
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi

PROJ="${PROJ:-demo_lcd_rgb}"
TARGET="${TARGET:-esp32c6}"
USE_IDF_WRAPPER="${USE_IDF_WRAPPER:-1}"
ALLOW_SUDO_CLEAN="${ALLOW_SUDO_CLEAN:-0}"

ESPPORT="${ESPPORT:-$("${ROOT}/scripts/find-port.sh" || true)}"
[[ -n "${ESPPORT}" ]] || { echo "ERR: ustaw ESPPORT=/dev/ttyUSBx lub /dev/ttyACMx" >&2; exit 2; }

ESPBAUD="${ESPBAUD:-460800}"
MONBAUD="${MONBAUD:-115200}"

PROJ_DIR="${ROOT}/firmware/projects/${PROJ}"
BUILD_DIR="${PROJ_DIR}/build"

# Pozwalamy składać defaults (łańcuch "a;b;c")
: "${SDKCONFIG_DEFAULTS:=}"

relpath(){ python3 - "$1" "$2" <<'PY'
import os,sys
try: print(os.path.relpath(sys.argv[1], sys.argv[2]))
except Exception: print(sys.argv[1])
PY
}
PROJ_PRETTY="$(relpath "${PROJ_DIR}" "${ROOT}")"

run_idf() {
  if [[ "${USE_IDF_WRAPPER}" == "1" ]]; then
    PROJ="${PROJ}" TARGET="${TARGET}" ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" "$@"
  else
    [[ -n "${IDF_PATH:-}" && -d "${IDF_PATH}" ]] || { echo "ERR: USE_IDF_WRAPPER=0 wymaga IDF_PATH" >&2; exit 20; }
    # shellcheck source=/dev/null
    . "${IDF_PATH}/export.sh" >/dev/null
    ( cd "${PROJ_DIR}" && idf.py "$@" )
  fi
}

safe_clean() {
  local path="$1"
  [[ ! -e "${path}" ]] && return 0
  rm -rf -- "${path}" >/dev/null 2>&1 && return 0
  echo "WARN: nie mogę usunąć '${path}' — próbuję naprawić prawa..." >&2
  chmod -R u+rwX "${path}" >/dev/null 2>&1 || true
  rm -rf -- "${path}" >/dev/null 2>&1 && return 0
  local uid myuid; uid="$(stat -c '%u' "${path}" 2>/dev/null || echo "")"; myuid="$(id -u)"
  if [[ -n "${uid}" && "${uid}" != "${myuid}" ]]; then
    echo "WARN: '${path}' ma właściciela uid=${uid} (pewnie root)." >&2
    if [[ "${ALLOW_SUDO_CLEAN}" == "1" && -x "$(command -v sudo || true)" ]]; then
      echo "INFO: używam sudo do chown+rm '${path}'." >&2
      sudo chown -R "$(id -un)":"$(id -gn)" "${path}" || true
      sudo chmod -R u+rwX "${path}" || true
      sudo rm -rf -- "${path}" || { echo "ERR: sudo rm -rf '${path}' nie powiódł się." >&2; exit 11; }
      return 0
    else
      echo "ERR: Brak zgody na sudo. Ręcznie wykonaj:" >&2
      echo "     sudo chown -R \"$(id -un)\":\"$(id -gn)\" \"${path}\" && rm -rf \"${path}\"" >&2
      exit 10
    fi
  fi
  rm -rf -- "${path}" || { echo "ERR: Nie mogę usunąć '${path}' (spróbuj ręcznie)." >&2; exit 12; }
}

ensure_target() {
  local cur=""
  [[ -f "${PROJ_DIR}/sdkconfig" ]] && cur="$(grep -E '^CONFIG_IDF_TARGET="' "${PROJ_DIR}/sdkconfig" | cut -d'"' -f2 || true)"
  if [[ -z "${cur}" || "${cur}" != "${TARGET}" ]]; then
    echo "set-target: want='${TARGET}' cur='${cur:-<none>}' → idf.py set-target"
    ( cd "${PROJ_DIR}" && run_idf set-target "${TARGET}" )
  else
    echo "set-target: OK (cur='${cur}') — pomijam"
  fi
}

# ===== Filtr monitora =========================================================
RAW_FILTER="${IDF_MONITOR_FILTER:-}"; RAW_FILTER="${RAW_FILTER//,/ }"; RAW_FILTER="${RAW_FILTER//;/ }"
read -r -a _TOKENS <<< "${RAW_FILTER}"
MON_FILTER_ARGS=()
for tok in "${_TOKENS[@]}"; do
  [[ -z "${tok}" ]] && continue
  if [[ "${tok}" != *:* || "$(tr -dc ':' <<< "${tok}")" != ":" ]]; then
    echo "ERR: zły token filtra: '${tok}' (TAG:LVL, np. '*:I' 'APP:W')" >&2; exit 3
  fi
  MON_FILTER_ARGS+=( --print_filter "${tok}" )
done

echo "Flash+Monitor: ${ESPPORT} (Ctrl+] wyjście)"
echo "Projekt: ${PROJ_PRETTY} (TARGET=${TARGET})"
echo "Monitor filter: ${RAW_FILTER:-(brak)}"
echo "Tryb: $([[ ${USE_IDF_WRAPPER} == 1 ]] && echo 'WRAPPER' || echo 'DIRECT')"

# ===== Reset (na życzenie) ====================================================
if [[ "${RESET_SDKCONFIG:-0}" == "1" ]]; then
  echo "RESET_SDKCONFIG=1 → czyszczę sdkconfig i build/"
  safe_clean "${PROJ_DIR}/sdkconfig"
  safe_clean "${BUILD_DIR}"
fi

# 1) set-target tylko gdy potrzeba
ensure_target

# 2) build
( cd "${PROJ_DIR}" && run_idf build )

# 3) flash_size sanity
WANT="$(grep -E '^CONFIG_ESPTOOLPY_FLASHSIZE="' "${PROJ_DIR}/sdkconfig.${TARGET}.defaults" | cut -d'"' -f2 || true)"
ARGS_FILE="${BUILD_DIR}/flash_args"; GOT=""
[[ -f "${ARGS_FILE}" ]] && GOT="$(grep -oE -- '--flash_size[[:space:]]+[^[:space:]]+' "${ARGS_FILE}" | awk '{print $2}' | head -n1 || true)"
echo "Pre-check flash_size: want='${WANT:-<none>}' got='${GOT:-<none}’"
if [[ -n "${WANT}" && "${WANT}" != "detect" && "${GOT}" != "${WANT}" && "${GOT}" != "detect" ]]; then
  echo "ERR: flash_size GOT='${GOT}', WANT='${WANT}' (z defaults)." >&2
  echo "     Uruchom z RESET_SDKCONFIG=1 lub usuń ${PROJ_DIR}/sdkconfig & ${BUILD_DIR}" >&2
  exit 6
fi

# 4) flash
( cd "${PROJ_DIR}" && run_idf -p "${ESPPORT}" -b "${ESPBAUD}" flash )

# 5) monitor
( cd "${PROJ_DIR}" && run_idf -p "${ESPPORT}" monitor --monitor-baud "${MONBAUD}" "${MON_FILTER_ARGS[@]}" "$@" )
