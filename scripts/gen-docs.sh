#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
docker run --rm -it   -v esp-idf-espressif:/root/.espressif   -v "${ROOT}:/work"   esp32-idf:5.3-docs bash -lc "cd /work && doxygen Doxyfile && echo 'OK: docs/html/index.html'"
