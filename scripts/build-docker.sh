#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
docker build -t esp32-idf:5.3-docs "${SCRIPT_DIR}/../Docker"
echo "OK: obraz Docker 'esp32-idf:5.3-docs' gotowy."
