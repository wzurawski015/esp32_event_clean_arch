#!/usr/bin/env bash
set -Eeuo pipefail
# shellcheck disable=SC1091
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.env.sh"

# Dockerfile selection (czołgowo odporne):
# 1) Docker/Dockerfile.idf-${IDF_TAG}   (np. Dockerfile.idf-5.5.2)
# 2) Docker/Dockerfile.idf              (jeden, “uniwersalny”)
# 3) Docker/Dockerfile.idf-5.5.1        (legacy fallback)
#
DOCKERFILE_CANDIDATES=(
  "${ROOT}/Docker/Dockerfile.idf-${IDF_TAG}"
  "${ROOT}/Docker/Dockerfile.idf"
  "${ROOT}/Docker/Dockerfile.idf-5.5.1"
)
DOCKERFILE=""
for f in "${DOCKERFILE_CANDIDATES[@]}"; do
  if [[ -f "${f}" ]]; then
    DOCKERFILE="${f}"
    break
  fi
done
[[ -n "${DOCKERFILE}" ]] || { echo "ERR: nie znaleziono Dockerfile.idf (sprawdz Docker/)" >&2; exit 2; }

case "$(uname -m)" in
  x86_64|amd64) WANT_ARCH="amd64" ;;
  aarch64|arm64) WANT_ARCH="arm64" ;;
  *)             WANT_ARCH="$(uname -m)" ;;
esac
WANT_OS="linux"

need_persist=0
if [[ "${IDF_DIGEST}" == "sha256:REPLACE_ME" || -z "${IDF_DIGEST}" ]]; then
  echo "INFO: autowykrywam digest dla ${WANT_OS}/${WANT_ARCH} (espressif/idf:v${IDF_TAG})…"

  # 1) Prefer: docker manifest + jq
  if command -v jq >/dev/null 2>&1; then
    IDF_DIGEST="$(docker manifest inspect "espressif/idf:v${IDF_TAG}" \
        | jq -r --arg os "${WANT_OS}" --arg arch "${WANT_ARCH}" \
          '.manifests[] | select(.platform.os==$os and .platform.architecture==$arch) | .digest' \
        | head -n1 || true)"
  fi

  # 2) Fallback: buildx imagetools (gdy brak jq)
  if [[ -z "${IDF_DIGEST}" ]]; then
    IDF_DIGEST="$(docker buildx imagetools inspect "espressif/idf:v${IDF_TAG}" 2>/dev/null \
      | awk -v want="${WANT_OS}/${WANT_ARCH}" '
          /^Platform:/ {plat=$2}
          /^Digest:/ && plat==want {print $2; exit}
      ' || true)"
  fi

  # 3) Ostateczny fallback: pierwszy manifest (wymaga jq)
  if [[ -z "${IDF_DIGEST}" ]] && command -v jq >/dev/null 2>&1; then
    IDF_DIGEST="$(docker manifest inspect "espressif/idf:v${IDF_TAG}" \
                  | jq -r '.manifests[0].digest' || true)"
  fi

  [[ "${IDF_DIGEST}" =~ ^sha256:[0-9a-f]{64}$ ]] || { echo "ERR: nie wykryłem sha256 dla v${IDF_TAG}"; exit 3; }
  echo "INFO: wykryty digest: ${IDF_DIGEST}"
  need_persist=1
fi

echo "Building ${IDF_IMAGE} (IDF ${IDF_TAG} @ ${IDF_DIGEST})"

# buildx (preferowane), lub klasyczny build jako fallback
if docker buildx version >/dev/null 2>&1; then
  docker buildx build --load \
    --build-arg IDF_TAG="${IDF_TAG}" \
    --build-arg IDF_DIGEST="${IDF_DIGEST}" \
    --label org.opencontainers.image.source="$(git -C "${ROOT}" config --get remote.origin.url || echo unknown)" \
    --label org.opencontainers.image.revision="$(git -C "${ROOT}" rev-parse --short=12 HEAD || echo unknown)" \
    -f "${DOCKERFILE}" \
    -t "${IDF_IMAGE}" "${ROOT}"
else
  echo "WARN: docker buildx nie znaleziony — używam klasycznego 'docker build'."
  docker build \
    --build-arg IDF_TAG="${IDF_TAG}" \
    --build-arg IDF_DIGEST="${IDF_DIGEST}" \
    -f "${ROOT}/Docker/Dockerfile.idf-5.5.1" \
    -t "${IDF_IMAGE}" "${ROOT}"
fi

if (( need_persist )); then
  tmp="${ROOT}/.env.tmp.$$"
  [[ -f "${ROOT}/.env" ]] || : > "${ROOT}/.env"
  awk -v d="${IDF_DIGEST}" '
    BEGIN{done=0}
    /^IDF_DIGEST=/{print "IDF_DIGEST=" d; done=1; next}
    {print}
    END{if(!done) print "IDF_DIGEST=" d}
  ' "${ROOT}/.env" > "${tmp}" && mv "${tmp}" "${ROOT}/.env"
  echo "INFO: zapisano IDF_DIGEST do .env"
fi

echo "OK: built ${IDF_IMAGE}"
