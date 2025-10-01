#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PROJ="${PROJ:-demo_lcd_rgb}"
TARGET="${TARGET:-esp32c6}"

PROJ="${PROJ}" TARGET="${TARGET}" "${ROOT}/scripts/idf.sh" menuconfig
