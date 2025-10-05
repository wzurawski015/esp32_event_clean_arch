#!/usr/bin/env bash
set -Eeuo pipefail

# ===== Repo root ==============================================================
if ROOT_GIT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null)"; then
  ROOT="${ROOT_GIT}"
else
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi

PROJ="${PROJ:-demo_lcd_rgb}"
TARGET="${TARGET:-esp32c6}"

PROJ_DIR="${ROOT}/firmware/projects/${PROJ}"
[[ -d "${PROJ_DIR}" ]] || { echo "ERR: brak projektu '${PROJ}' w firmware/projects" >&2; exit 2; }

# ===== Docker image & HOME ====================================================
# Użyj swojego obrazu (albo oficjalnego espressif/idf:v5.5.1 przez ENV).
IMAGE="${IDF_IMAGE:-esp32-idf:5.5.1-docs}"
# Walidacja nazwy obrazu (eliminuje 'invalid reference format')
if [[ -z "${IMAGE}" ]] || printf '%s' "${IMAGE}" | grep -q '[[:space:][:cntrl:]]'; then
  echo "ERR: IDF_IMAGE='${IMAGE}' jest puste/ma białe/sterujące znaki" >&2
  printf 'Hex: '; printf '%s' "${IMAGE}" | od -An -t x1; echo
  exit 91
fi
if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
  echo "WARN: obraz '${IMAGE}' nie istnieje lokalnie – Docker spróbuje pobrać." >&2
fi

# Repo-lokalny HOME (żeby artefakty nie były root-owe)
HOST_HOME="${ROOT}/.idf-docker-home"
mkdir -p "${HOST_HOME}/.espressif" "${HOST_HOME}/.cache/ccache" "${HOST_HOME}/.ccache"

# ===== Port szeregowy & grupy =================================================
PORT_DEV=()
GROUP_ADD=()
if [[ -n "${ESPPORT:-}" && -e "${ESPPORT}" ]]; then
  PORT_DEV=(--device="${ESPPORT}:${ESPPORT}")
  if DEV_GID="$(stat -c '%g' "${ESPPORT}" 2>/dev/null)"; then
    GROUP_ADD=(--group-add "${DEV_GID}")
  fi
elif [[ -n "${ESPPORT:-}" ]]; then
  echo "WARN: ESPPORT='${ESPPORT}' nie istnieje – pomijam mapowanie." >&2
fi

# ===== TTY & user =============================================================
TTY=(); [[ -t 1 ]] && TTY=(-it)
DOCKER_USER=(-u "$(id -u)":"$(id -g)")

# ===== uruchomienie ===========================================================
docker run --rm "${TTY[@]}" \
  "${DOCKER_USER[@]}" \
  "${GROUP_ADD[@]}" \
  -e HOME="/home/builder" \
  -e LANG="C.UTF-8" -e LC_ALL="C.UTF-8" \
  -e TERM="${TERM:-xterm-256color}" -e COLUMNS="${COLUMNS:-120}" -e LINES="${LINES:-40}" \
  -e IDF_CCACHE_ENABLE=1 \
  -e CCACHE_DIR="/home/builder/.ccache" \
  -e IDF_TARGET="${TARGET}" -e PROJ="${PROJ}" -e TARGET="${TARGET}" \
  -v "${ROOT}:${ROOT}" \
  -v "${HOST_HOME}:/home/builder" \
  "${PORT_DEV[@]}" \
  ${IDF_EXTRA_DOCKER_ARGS:-} \
  "${IMAGE}" \
  bash -lc '
    set -Eeuo pipefail

    # HOME i cache (musi być zapisywalny)
    mkdir -p "$HOME/.espressif" "$HOME/.cache/ccache" "$HOME/.ccache"
    if [[ ! -w "$HOME/.ccache" ]]; then
      echo "ERR: $HOME/.ccache nie jest zapisywalny (ccache)"; ls -ld "$HOME" "$HOME/.ccache"; exit 93
    fi

    # IDF środowisko
    if [[ -z "${IDF_PATH:-}" || ! -d "${IDF_PATH:-}" ]]; then
      echo "ERR: brak IDF_PATH w obrazie; sprawdź obraz Docker." >&2; exit 90
    fi
    . "${IDF_PATH}/export.sh" >/dev/null

    # (workaround) git safe.directory dla repo w obrazie
    git config --global --add safe.directory /opt/esp/idf            >/dev/null 2>&1 || true
    git config --global --add safe.directory /opt/esp/idf/components/openthread/openthread >/dev/null 2>&1 || true

    cd "'"${PROJ_DIR//\'/\'\\\'\'}"'"

    # ensure_target tylko gdy potrzeba
    CUR=$(grep -E "^CONFIG_IDF_TARGET=\"" sdkconfig 2>/dev/null | sed -E "s/.*\"(.+)\".*/\1/" || true)
    if [[ "${CUR:-}" != "'"${TARGET}"'" ]]; then
      idf.py set-target "'"${TARGET}"'"
    fi

    # wykonaj dokładnie to, co przyszło
    idf.py "$@"
  ' -- "$@"
