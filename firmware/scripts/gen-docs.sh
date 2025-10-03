#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
doxygen Doxyfile
echo "OK: dokumentacja wygenerowana → firmware/docs/html/index.html"
