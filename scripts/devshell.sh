#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

tty_args=(); [[ -t 0 ]] && tty_args+=(-t)
exec docker run --rm -i "${tty_args[@]}" \
  --user "$(id -u)":"$(id -g)" \
  -e HOME="${DOCKER_HOME}" \
  -v "${ROOT}:/work" \
  -v "${DOCKER_HOME_MOUNT}:${DOCKER_HOME}:rw" \
  -w "/work" \
  "${IDF_IMAGE}" bash
