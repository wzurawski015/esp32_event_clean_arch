#!/usr/bin/env bash
set -euo pipefail
# Tworzymy wolumeny na narzędzia IDF i cache kompilacji
docker volume create esp-idf-espressif >/dev/null
docker volume create esp-idf-ccache >/dev/null
echo "OK: wolumeny 'esp-idf-espressif' i 'esp-idf-ccache' gotowe."
