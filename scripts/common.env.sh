#!/usr/bin/env bash
set -Eeuo pipefail

# --------- Wyznacz ROOT repo, eksportuj PRZED czytaniem .env ---------
if ROOT_GIT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null)"; then
  ROOT="${ROOT_GIT}"
else
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
export ROOT

# --------- Loader .env: ENV ma pierwszeństwo, ekspansja z ochroną przed -u ---------
dotenv() {
  local file="${1:-}"
  [[ -f "${file}" ]] || return 0

  # Zapamiętaj, czy -u było włączone, i tymczasowo je wyłącz
  local had_u=0
  case "$-" in *u*) had_u=1; set +u;; esac

  while IFS= read -r line || [[ -n "${line}" ]]; do
    line="${line%$'\r'}"                           # zdejmij CR (Windows)
    [[ -z "${line}" || "${line}" =~ ^[[:space:]]*# ]] && continue

    if [[ "${line}" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
      local key="${line%%=*}"
      local val="${line#*=}"

      # utnij komentarz inline ' #...' (prosto i skutecznie dla naszych wartości)
      # UWAGA: jeśli kiedyś będziesz potrzebował znaku '# ' w wartości, użyj cudzysłowów w .env
      case "${val}" in
        *" #"*) val="${val%% \#*}";;
        *$'\t#'*) val="${val%%$'\t'#*}";;
      esac
      # usuń końcowe spacje
      val="${val%"${val##*[![:space:]]}"}"

      # Rozwiń referencje (ROOT jest już ustawiony, wcześniejsze klucze też)
      local val_expanded
      eval "val_expanded=\"${val}\""

      # Nie nadpisuj jeśli zmienna jest już w środowisku
      if [[ -z "${!key+x}" ]]; then
        printf -v "${key}" '%s' "${val_expanded}"
        export "${key}"
      fi
    fi
  done < "${file}"

  ((had_u)) && set -u  # przywróć -u, jeśli było
}

dotenv "${ROOT}/.env"

# --------- Domyślne wartości, gdy czegoś nie było w ENV/.env ---------
: "${IDF_TAG:=5.5.1}"
: "${IDF_DIGEST:=sha256:REPLACE_ME}"
: "${IDF_IMAGE:=esp32-idf:${IDF_TAG}-docs}"

: "${PROJ:=demo_lcd_rgb}"
: "${TARGET:=esp32c6}"
: "${ESPBAUD:=921600}"
: "${MONBAUD:=115200}"

: "${DOCKER_HOME:=/home/esp}"
: "${DOCKER_HOME_MOUNT:=${ROOT}/.idf-docker-home}"

export ROOT IDF_TAG IDF_DIGEST IDF_IMAGE PROJ TARGET CONSOLE ESPBAUD MONBAUD DOCKER_HOME DOCKER_HOME_MOUNT

