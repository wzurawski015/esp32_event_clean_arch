#!/usr/bin/env bash
set -Eeuo pipefail

# --------- Ustal ROOT repo (nawet uruchamiane z dowolnego katalogu) ---------
if ROOT_GIT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null)"; then
  ROOT="${ROOT_GIT}"
else
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
# EKSPORTUJ ROOT PRZED WCZYTANIEM .env -> aby ${ROOT} w .env działało
export ROOT

# --------- Prosty loader .env (ENV ma pierwszeństwo nad .env) ---------
dotenv() {
  local file="${1:-}"
  [[ -f "${file}" ]] || return 0
  # Czytaj linia po linii
  while IFS= read -r line || [[ -n "${line}" ]]; do
    # Usuń CR z końca (gdy plik powstał na Windows)
    line="${line%$'\r'}"
    # Puste linie i komentarze
    [[ -z "${line}" || "${line}" =~ ^[[:space:]]*# ]] && continue
    # Klucz=wartość
    if [[ "${line}" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
      local key="${line%%=*}" val="${line#*=}"
      # Rozwiń referencje typu ${FOO} w KONTEKŚCIE TEJ POWŁOKI
      # (widzimy już tutaj ${ROOT}, ${IDF_TAG} itd.)
      # shellcheck disable=SC2034
      eval "val_expanded=\"${val}\""
      # Nie nadpisuj, jeśli już ustawione w środowisku
      [[ -z "${!key+x}" ]] && eval "export ${key}=\"\${val_expanded}\""
    fi
  done < "${file}"
}

dotenv "${ROOT}/.env"

# --------- Domyślne wartości (jeśli brak w ENV/.env) ---------
: "${IDF_TAG:=5.5.1}"
: "${IDF_DIGEST:=sha256:REPLACE_ME}"
: "${IDF_IMAGE:=esp32-idf:${IDF_TAG}-docs}"

: "${PROJ:=demo_lcd_rgb}"
: "${TARGET:=esp32c6}"
: "${ESPBAUD:=921600}"
: "${MONBAUD:=115200}"

: "${DOCKER_HOME:=/home/esp}"
: "${DOCKER_HOME_MOUNT:=${ROOT}/.idf-docker-home}"

# Eksport na koniec (gdyby ktoś sourował ten plik „na zimno”)
export ROOT IDF_TAG IDF_DIGEST IDF_IMAGE PROJ TARGET ESPBAUD MONBAUD DOCKER_HOME DOCKER_HOME_MOUNT
