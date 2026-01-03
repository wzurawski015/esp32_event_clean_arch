#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

# Użycie:
#   scripts/gen-docs.sh                # użyje ${ROOT}/Doxyfile
#   scripts/gen-docs.sh path/to/Doxyfile
#   DOXYFILE=path/to/Doxyfile scripts/gen-docs.sh
doxyfile="${1:-${DOXYFILE:-${ROOT}/Doxyfile}}"

if [[ ! -f "${doxyfile}" ]]; then
  echo "ERR: Doxyfile nie znaleziony: ${doxyfile}" >&2
  exit 3
fi
# Doxyfile musi być w repo (bo montujemy ${ROOT} -> /work)
if [[ "${doxyfile}" != "${ROOT}/"* ]]; then
  echo "ERR: Doxyfile musi leżeć wewnątrz repo (${ROOT})." >&2
  exit 4
fi
# Ścieżka względna w kontenerze (od /work)
doxy_rel="${doxyfile#${ROOT}/}"

# Przygotuj HOME kontenera (własność UID:GID; brak root:root)
mkdir -p "${DOCKER_HOME_MOUNT}/.espressif" \
         "${DOCKER_HOME_MOUNT}/.cache/ccache" \
         "${DOCKER_HOME_MOUNT}/.ccache"

exec docker run --rm -i "${tty_args[@]}" --entrypoint "" \
  --user "$(id -u)":"$(id -g)" \
  -e HOME="${DOCKER_HOME}" \
  -v "${ROOT}:/work" \
  -v "${DOCKER_HOME_MOUNT}:${DOCKER_HOME}:rw" \
  -w /work "${IDF_IMAGE}" bash -lc '
    set -Eeuo pipefail
    DOXYFILE_REL="$1"

    # Ciche wczytanie środowiska IDF (nie jest wymagane, ale ujednolica PATH)
    if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/export.sh" ]]; then
      . "${IDF_PATH}/export.sh" >/dev/null 2>&1 || true
    fi

    command -v doxygen >/dev/null || { echo "ERR: doxygen nie znaleziony w obrazie." >&2; exit 2; }
    if ! command -v dot >/dev/null 2>&1; then
      echo "WARN: graphviz/dot niedostępny — diagramy będą wyłączone." >&2
    fi

    doxygen "${DOXYFILE_REL}"

    # Typowa lokalizacja
    if [[ -e docs/html/index.html ]]; then
      echo "OK: docs/html/index.html"
    else
      echo "OK: dokumentacja wygenerowana (sprawdź OUTPUT_DIRECTORY w Doxyfile)"
    fi
  ' -- "${doxy_rel}"
