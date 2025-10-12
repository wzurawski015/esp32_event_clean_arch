#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

ok(){ printf "OK   - %s\n" "$*"; }
wr(){ printf "WARN - %s\n" "$*" >&2; }
er(){ printf "ERR  - %s\n" "$*" >&2; FAILED=1; }

FAILED=0

# --- Docker ------------------------------------------------------------------------
command -v docker >/dev/null 2>&1 || { er "Brak 'docker' na PATH"; exit 1; }
docker info    >/dev/null 2>&1 && ok "docker działa"             || wr "docker info nie działa (daemon/uprawnienia?)"
docker buildx version >/dev/null 2>&1 && ok "docker buildx obecny" || wr "brak buildx (fallback zadziała, ale wolniej)"
command -v jq >/dev/null 2>&1 && ok "jq obecny (autopin digestu szybki)" || wr "brak jq (autopin użyje imagetools/awk)"

# --- Pin obrazu (single source of truth) ------------------------------------------
printf "INFO - IDF_IMAGE=%s  IDF_TAG=%s  IDF_DIGEST=%s\n" "$IDF_IMAGE" "$IDF_TAG" "$IDF_DIGEST"
# 1) .env ma oba pola?
[[ -n "${IDF_TAG:-}"     ]] || er "IDF_TAG niewypełniony w .env"
[[ -n "${IDF_DIGEST:-}"  ]] || er "IDF_DIGEST niewypełniony w .env (sha256:...)"

# 2) Dockerfile korzysta z ARG i z @${IDF_DIGEST} we FROM?
DKR="${ROOT}/Docker/Dockerfile.idf-5.5.1"
if [[ -f "$DKR" ]]; then
  grep -qE '^ARG[[:space:]]+IDF_TAG'    "$DKR" || er "Dockerfile: brak 'ARG IDF_TAG'"
  grep -qE '^ARG[[:space:]]+IDF_DIGEST' "$DKR" || er "Dockerfile: brak 'ARG IDF_DIGEST'"
  if grep -qE '^FROM[[:space:]]+espressif/idf:v\$\{IDF_TAG\}@\$\{IDF_DIGEST\}' "$DKR"; then
    ok "Dockerfile używa FROM ... v\${IDF_TAG}@\${IDF_DIGEST}"
  else
    er "Dockerfile: FROM nie wykorzystuje @\${IDF_DIGEST} (pin jest wymagany)"
  fi
else
  er "Brak pliku Docker/Dockerfile.idf-5.5.1"
fi

# 3) Czy obraz lokalny istnieje (zbudowany twoim skryptem)?
docker image inspect "$IDF_IMAGE" >/dev/null 2>&1 && ok "obraz ${IDF_IMAGE} jest lokalnie" || wr "obrazu brak lokalnie (uruchom scripts/build-docker.sh)"

# --- HOME bind-mount (zapisywalność) ----------------------------------------------
for d in ".espressif" ".cache/ccache" ".ccache"; do
  p="${DOCKER_HOME_MOUNT}/${d}"; mkdir -p "$p"
  [[ -w "$p" ]] && ok "writable ${p}" || er "brak zapisu: ${p}"
done

# --- Serial (opcjonalnie) ---------------------------------------------------------
if [[ -n "${ESPPORT:-}" ]]; then
  if [[ -e "${ESPPORT}" ]]; then
    ok "ESPPORT istnieje: ${ESPPORT}"
    [[ -w "${ESPPORT}" ]] && ok "ESPPORT zapisywalny" || wr "ESPPORT nie jest zapisywalny (skrypt doda --group-add dla GID)"
  else
    wr "ESPPORT nie istnieje: ${ESPPORT}"
  fi
else
  wr "ESPPORT nie ustawiony; auto-detekcja zadziała w flash-monitor.sh"
fi

# --- Wymagane defaults dla aktywnego projektu -------------------------------------
proj_dir="${ROOT}/firmware/projects/${PROJ}"
[[ -d "${proj_dir}" ]] || er "Brak katalogu projektu: ${proj_dir}"

need=(
  "${proj_dir}/sdkconfig.defaults"
  "${proj_dir}/sdkconfig.console.${CONSOLE:-uart}.defaults"
  "${proj_dir}/sdkconfig.${TARGET}.defaults"
)

missing=()
for f in "${need[@]}"; do
  if [[ -f "${f}" ]]; then
    ok "defaults: ${f}"
  else
    missing+=("${f}")
  fi
done

if (( ${#missing[@]} > 0 )); then
  wr "Brakujące pliki defaults:"
  for f in "${missing[@]}"; do echo "  - ${f}"; done
  if [[ "${DOCTOR_AUTOFIX_DEFAULTS:-0}" == "1" ]]; then
    for f in "${missing[@]}"; do
      mkdir -p "$(dirname "$f")"
      {
        echo "# Auto-generated empty defaults (doctor.sh) — utrzymuj w repo"
        echo "# Plik może pozostać pusty, ale musi istnieć (SDKCONFIG_DEFAULTS go wskazuje)."
      } > "${f}"
      ok "utworzono: ${f}"
    done
  else
    er "Utwórz brakujące pliki (choćby puste) lub uruchom: DOCTOR_AUTOFIX_DEFAULTS=1 ./scripts/doctor.sh"
  fi
fi

# --- Doxygen sanity (opcja) --------------------------------------------------------
for DX in "${ROOT}/Doxyfile" "${ROOT}/firmware/Doxyfile"; do
  [[ -f "$DX" ]] || continue
  if grep -q '^DOT_TRANSPARENT' "$DX"; then
    wr "Doxyfile: DOT_TRANSPARENT jest przestarzałe — usuń tę linię (doxygen -u) -> ${DX}"
  fi
done

# --- Self-test idf ----------------------------------------------------------------
scripts/idf.sh --version >/dev/null 2>&1 && ok "idf.sh --version OK" || er "idf.sh --version nie przeszło"

if (( FAILED )); then
  echo "Doctor: WYKRYTO PROBLEMY." >&2
  exit 2
fi
echo "Doctor: OK."
