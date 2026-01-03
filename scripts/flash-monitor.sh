#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

echo "Flash+Monitor: przygotowuję projekt '${PROJ}' (TARGET=${TARGET}, CONSOLE=${CONSOLE:-uart})"
proj_dir="${ROOT}/firmware/projects/${PROJ}"
build_dir="${proj_dir}/build"
[[ -d "${proj_dir}" ]] || { echo "ERR: brak katalogu projektu: ${proj_dir}" >&2; exit 3; }

# Wykryj port, jeśli nie podano
if [[ -z "${ESPPORT:-}" ]]; then
  ESPPORT="$("${ROOT}/scripts/find-port.sh")" || { echo "ERR: nie wykryłem portu!" >&2; exit 2; }
fi

# Twarda walidacja portu (żeby nie iść dalej w „złe /dev/…”)
if [[ ! -e "${ESPPORT}" ]]; then
  echo "ERR: ESPPORT='${ESPPORT}' nie istnieje. Sprawdź usbipd/WSL lub podaj poprawny port." >&2
  exit 2
fi

echo "Używam portu: ${ESPPORT}"

# Zadbaj o zgodność targetu (bez resetu clean — szybciej)
ensure_target() {
  local cur=""
  if [[ -f "${proj_dir}/sdkconfig" ]]; then
    cur="$(grep -E '^CONFIG_IDF_TARGET="' "${proj_dir}/sdkconfig" | cut -d'"' -f2 || true)"
  fi
  if [[ -z "${cur}" || "${cur}" != "${TARGET}" ]]; then
    echo "set-target: want='${TARGET}' cur='${cur:-<none>}' → idf.py set-target"
    ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" set-target "${TARGET}"
  else
    echo "set-target: OK (cur='${cur}') — pomijam"
  fi
}

# Opcjonalny „twardy” reset konfiguracji: clean + nowy sdkconfig z defaults
if [[ "${RESET_SDKCONFIG:-0}" == "1" ]]; then
  echo "RESET_SDKCONFIG=1 → fullclean + set-target (${TARGET})"
  ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" fullclean
  ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" set-target "${TARGET}"
else
  ensure_target
fi

# Kompilacja
ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" build

# Pre-check flash_size (pomocniczo; nie blokuje, tylko ostrzega)
if [[ -f "${proj_dir}/sdkconfig.${TARGET}.defaults" && -f "${build_dir}/flash_args" ]]; then
  want="$(grep -E '^CONFIG_ESPTOOLPY_FLASHSIZE="' "${proj_dir}/sdkconfig.${TARGET}.defaults" | cut -d'"' -f2 || true)"
  got="$(grep -oE -- '--flash_size[[:space:]]+[^[:space:]]+' "${build_dir}/flash_args" | awk '{print $2}' | head -n1 || true)"
  # sanitizacja ewentualnych artefaktów typu 'detect>'
  got="${got//\\>/}"; got="${got%>}"
  echo "Pre-check flash_size: want='${want:-<none>}' got='${got:-<none>}'"
  if [[ -n "${want}" && "${want}" != "detect" && -n "${got}" && "${got}" != "detect" && "${got}" != "${want}" ]]; then
    echo "WARN: flash_size różne: build='${got}', defaults='${want}'. Jeśli zmieniałeś target/partycje – uruchom z RESET_SDKCONFIG=1." >&2
  fi
fi

# Flash (baud do flash)
flash_baud="${ESPBAUD:-460800}"
ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${flash_baud}" flash

# Filtry monitora (np. IDF_MONITOR_FILTER="APP:W,*:I")
monitor_filter_args=()
if [[ -n "${IDF_MONITOR_FILTER:-}" ]]; then
  IFS=',; ' read -r -a tokens <<< "${IDF_MONITOR_FILTER}"
  for tok in "${tokens[@]}"; do
    [[ -z "${tok}" ]] && continue
    if [[ "${tok}" != *:* || "$(tr -dc ':' <<< "${tok}")" != ":" ]]; then
      echo "ERR: zły token filtra: '${tok}' (użyj formatu TAG:LVL, np. '*:I' 'APP:W')" >&2; exit 4
    fi
    monitor_filter_args+=( --print_filter "${tok}" )
  done
fi

# Monitor (baud do monitora)
mon_baud="${MONBAUD:-115200}"
echo "Start monitora (baud ${mon_baud}; Ctrl+] = wyjście)…"
ESPPORT="${ESPPORT}" "${ROOT}/scripts/idf.sh" -p "${ESPPORT}" -b "${mon_baud}" monitor "${monitor_filter_args[@]}" "$@"
