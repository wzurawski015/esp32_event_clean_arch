#!/usr/bin/env bash
set -Eeuo pipefail

# ROOT repo
if ROOT_GIT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null)"; then
  ROOT="${ROOT_GIT}"
else
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi

dotenv() {
  local file="${1:-}"
  [[ -f "${file}" ]] || return 0
  while IFS= read -r line || [[ -n "${line}" ]]; do
    [[ -z "${line}" || "${line}" =~ ^[[:space:]]*# ]] && continue
    if [[ "${line}" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
      local key="${line%%=*}" val="${line#*=}"
      # shellcheck disable=SC2016
      val="$(bash -c 'eval "printf \"%s\" \"$1\""' _ "${val}")"
      [[ -z "${!key+x}" ]] && eval "export ${key}=\"${val}\""
    fi
  done < "${file}"
}

dotenv "${ROOT}/.env"

: "${IDF_TAG:=5.5.1}"
: "${IDF_DIGEST:=sha256:REPLACE_ME}"
: "${IDF_IMAGE:=esp32-idf:${IDF_TAG}-docs}"

: "${PROJ:=demo_lcd_rgb}"
: "${TARGET:=esp32c6}"
: "${ESPBAUD:=921600}"
: "${MONBAUD:=115200}"

: "${DOCKER_HOME:=/home/esp}"
: "${DOCKER_HOME_MOUNT:=${ROOT}/.idf-docker-home}"

export ROOT IDF_TAG IDF_DIGEST IDF_IMAGE PROJ TARGET ESPBAUD MONBAUD DOCKER_HOME DOCKER_HOME_MOUNT
