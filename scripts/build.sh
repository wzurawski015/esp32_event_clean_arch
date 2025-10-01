#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PROJ="${PROJ:-demo_hello_ev}"
TARGET="${TARGET:-esp32c6}"

PROJ="${PROJ}" TARGET="${TARGET}" "${ROOT}/scripts/idf.sh" build
