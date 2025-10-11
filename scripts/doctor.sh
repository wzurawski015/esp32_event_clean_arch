#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

ok(){ printf "OK   - %s\n" "$*"; }
wr(){ printf "WARN - %s\n" "$*" >&2; }
er(){ printf "ERR  - %s\n" "$*" >&2; FAILED=1; }

FAILED=0

# Docker
command -v docker >/dev/null 2>&1 || { er "Brak 'docker' na PATH"; exit 1; }
docker info >/dev/null 2>&1 && ok "docker działa" || wr "docker info nie działa (daemon/uprawnienia?)"
docker buildx version >/dev/null 2>&1 && ok "docker buildx obecny" || wr "brak buildx (fallback zadziała, ale wolniej)"

# jq (opcjonalnie, lepszy autopin digestu)
command -v jq >/dev/null 2>&1 && ok "jq obecny (autopin digestu szybki)" || wr "brak jq (autopin użyje imagetools/awk)"

# Obraz
printf "INFO - IDF_IMAGE=%s  IDF_TAG=%s  IDF_DIGEST=%s\n" "$IDF_IMAGE" "$IDF_TAG" "$IDF_DIGEST"
docker image inspect "$IDF_IMAGE" >/dev/null 2>&1 && ok "obraz ${IDF_IMAGE} jest lokalnie" || wr "obrazu brak lokalnie (uruchom scripts/build-docker.sh)"

# HOME mount
for d in ".espressif" ".cache/ccache" ".ccache"; do
  p="${DOCKER_HOME_MOUNT}/${d}"; mkdir -p "$p"
  [[ -w "$p" ]] && ok "writable ${p}" || er "brak zapisu: ${p}"
done

# Serial
if [[ -n "${ESPPORT:-}" ]]; then
  if [[ -e "${ESPPORT}" ]]; then
    ok "ESPPORT istnieje: ${ESPPORT}"
    [[ -w "${ESPPORT}" ]] && ok "ESPPORT zapisywalny" || wr "ESPPORT nie jest zapisywalny (sprawdź grupę urządzenia; skrypt dodaje --group-add dla GID)"
  else
    wr "ESPPORT nie istnieje: ${ESPPORT}"
  fi
else
  wr "ESPPORT nie ustawiony; auto-detekcja zadziała w flash-monitor.sh"
fi

# Self-test idf
scripts/idf.sh --version >/dev/null 2>&1 && ok "idf.sh --version OK" || er "idf.sh --version nie przeszło"

if (( FAILED )); then
  echo "Doctor: WYKRYTO PROBLEMY." >&2
  exit 2
fi
echo "Doctor: OK."
