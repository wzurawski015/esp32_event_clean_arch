#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

# --- Projekt w repo ----------------------------------------------------------------
proj_dir="${ROOT}/firmware/projects/${PROJ}"
[[ -d "${proj_dir}" ]] || { echo "ERR: brak katalogu projektu: ${proj_dir}" >&2; exit 3; }

# --- Przygotuj hostowy HOME kontenera (żeby nic nie było root:root) ----------------
mkdir -p "${DOCKER_HOME_MOUNT}/.espressif" \
         "${DOCKER_HOME_MOUNT}/.cache/ccache" \
         "${DOCKER_HOME_MOUNT}/.ccache"

# --- Argumenty docker run -----------------------------------------------------------
tty_args=()
[[ -t 0 ]] && tty_args+=(-t)   # -t tylko jeśli mamy TTY; -i zawsze (monitor potrzebuje STDIN)

docker_args=(
  --rm -i "${tty_args[@]}"
  --user "$(id -u)":"$(id -g)"
  -e HOME="${DOCKER_HOME}"
  -e LANG="C.UTF-8" -e LC_ALL="C.UTF-8" -e TERM="${TERM:-xterm-256color}"
  -e IDF_CCACHE_ENABLE=1
  -e CCACHE_DIR="${DOCKER_HOME}/.ccache"
  -e CCACHE_COMPRESS=1
  -e CCACHE_MAXSIZE=5G
  -e IDF_TARGET="${TARGET}"
  -v "${ROOT}:/work"
  -v "${DOCKER_HOME_MOUNT}:${DOCKER_HOME}:rw"
  -w "/work/firmware/projects/${PROJ}"
)

# Wymiary terminala (ładniejsze REPL/monitor)
[[ -n "${COLUMNS:-}" ]] && docker_args+=(-e "COLUMNS=${COLUMNS}")
[[ -n "${LINES:-}"   ]] && docker_args+=(-e "LINES=${LINES}")

# Przekaż istotne ENV do środka
for v in PROJ TARGET CONSOLE ESPBAUD MONBAUD IDF_MONITOR_FILTER; do
  [[ -n "${!v:-}" ]] && docker_args+=(-e "${v}=${!v}")
done

# UART: urządzenie + GID grupy urządzenia (RW bez privileged)
if [[ -n "${ESPPORT:-}" && -e "${ESPPORT}" ]]; then
  docker_args+=(--device="${ESPPORT}:${ESPPORT}" -e "ESPPORT=${ESPPORT}")
  if gid="$(stat -c '%g' "${ESPPORT}" 2>/dev/null)"; then
    docker_args+=(--group-add "${gid}")
  elif gid="$(stat -f '%g' "${ESPPORT}" 2>/dev/null)"; then # macOS/BSD fallback
    docker_args+=(--group-add "${gid}")
  fi
elif [[ -n "${ESPPORT:-}" ]]; then
  echo "WARN: ESPPORT='${ESPPORT}' nie istnieje – pomijam mapowanie." >&2
fi

# Dodatkowe argumenty do `docker run` (np. --network host)
if [[ -n "${IDF_EXTRA_DOCKER_ARGS:-}" ]]; then
  # shellcheck disable=SC2206
  docker_args+=(${IDF_EXTRA_DOCKER_ARGS})
fi

# --- Uruchomienie wewnątrz kontenera (ciche export.sh + sanity) ---------------------
# Uwaga: --entrypoint "" wycisza domyślny entrypoint obrazu (czyli blok „Activating ESP‑IDF…”).
exec docker run --entrypoint "" "${docker_args[@]}" "${IDF_IMAGE}" bash -lc '
  set -Eeuo pipefail

  : "${IDF_PATH:?ERR: brak IDF_PATH w obrazie}"

  # Cache i HOME (muszą być zapisywalne)
  mkdir -p "$HOME/.ccache" "$HOME/.cache/ccache" "$HOME/.espressif"
  [[ -w "$HOME/.ccache" ]] || { echo "ERR: $HOME/.ccache nie jest zapisywalny"; exit 93; }

  # Ciche załadowanie środowiska IDF (eliminuje „Activating ESP‑IDF …” w logach)
  . "${IDF_PATH}/export.sh" >/dev/null

  # Bezpieczniki dla zmontowanych repozytoriów (na wszelki wypadek)
  git config --global --add safe.directory /opt/esp/idf >/dev/null 2>&1 || true
  git config --global --add safe.directory /opt/esp/idf/components/openthread/openthread >/dev/null 2>&1 || true

  cd "/work/firmware/projects/${PROJ}"
  idf.py "$@"
' -- "$@"
