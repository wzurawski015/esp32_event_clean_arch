#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

# Katalog projektu wewnątrz repo
proj_dir="${ROOT}/firmware/projects/${PROJ}"
[[ -d "${proj_dir}" ]] || { echo "ERR: brak katalogu projektu: ${proj_dir}" >&2; exit 3; }

# Przygotowanie argumentów Dockera
tty_args=()
[[ -t 0 ]] && tty_args+=(-t)   # TTY tylko gdy interaktywnie; -i dajemy zawsze (monitor potrzebuje STDIN)

docker_args=(
  --rm -i "${tty_args[@]}"
  --user "$(id -u)":"$(id -g)"
  -e HOME="${DOCKER_HOME}"
  -e IDF_CCACHE_ENABLE=1
  -e IDF_TARGET="${TARGET}"
  -v "${ROOT}:/work"
  -v "${DOCKER_HOME_MOUNT}:${DOCKER_HOME}:rw"
  -w "/work/firmware/projects/${PROJ}"
)

# Przekaż podstawowe ENV do środka (u Ciebie CMake/CMakeLists używa m.in. CONSOLE)
for v in PROJ TARGET CONSOLE ESPBAUD MONBAUD; do
  [[ -n "${!v:-}" ]] && docker_args+=(-e "${v}=${!v}")
done

# UART: device + GID grupy urządzenia (aby mieć RW bez privileged)
if [[ -n "${ESPPORT:-}" && -e "${ESPPORT}" ]]; then
  docker_args+=(--device="${ESPPORT}:${ESPPORT}")
  if gid="$(stat -c '%g' "${ESPPORT}" 2>/dev/null)"; then
    docker_args+=(--group-add "${gid}")
  elif gid="$(stat -f '%g' "${ESPPORT}" 2>/dev/null)"; then # macOS/BSD fallback
    docker_args+=(--group-add "${gid}")
  fi
  docker_args+=(-e "ESPPORT=${ESPPORT}")
fi

# Przydatne zmienne terminala (ładniejsze REPL/monitor)
[[ -n "${TERM:-}"    ]] && docker_args+=(-e "TERM=${TERM}")
[[ -n "${COLUMNS:-}" ]] && docker_args+=(-e "COLUMNS=${COLUMNS}")
[[ -n "${LINES:-}"   ]] && docker_args+=(-e "LINES=${LINES}")

# Opcjonalny dopalacz: dodatkowe argumenty do docker run (np. --network host)
if [[ -n "${IDF_EXTRA_DOCKER_ARGS:-}" ]]; then
  # shellcheck disable=SC2206
  docker_args+=(${IDF_EXTRA_DOCKER_ARGS})
fi

# Uruchom dokładnie to, co podano w linii poleceń
exec docker run "${docker_args[@]}" "${IDF_IMAGE}" idf.py "$@"
