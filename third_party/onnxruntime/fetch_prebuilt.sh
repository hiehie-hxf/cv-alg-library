#!/usr/bin/env bash
set -euo pipefail

VERSION="1.20.1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM="$(uname -s)"
MACHINE="$(uname -m)"

if [[ "${SYSTEM}" == "Darwin" && "${MACHINE}" == "arm64" ]]; then
  PLATFORM="macos-arm64"
  ARCHIVE="onnxruntime-osx-arm64-${VERSION}.tgz"
  URL="https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/${ARCHIVE}"
  SHA256="b678fc3c2354c771fea4fba420edeccfba205140088334df801e7fc40e83a57a"
else
  echo "No audited prebuilt package configured for ${SYSTEM}/${MACHINE}." >&2
  echo "Provide an SDK with -DCVSDK_ONNXRUNTIME_ROOT=/absolute/path." >&2
  exit 2
fi

DEST="${ROOT_DIR}/prebuilt/${PLATFORM}/${VERSION}"
if [[ -f "${DEST}/include/onnxruntime_cxx_api.h" ]]; then
  echo "ONNX Runtime already present: ${DEST}"
  exit 0
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT
curl -fL --retry 2 -o "${TMP_DIR}/${ARCHIVE}" "${URL}"
ACTUAL="$(shasum -a 256 "${TMP_DIR}/${ARCHIVE}" | awk '{print $1}')"
if [[ "${ACTUAL}" != "${SHA256}" ]]; then
  echo "SHA-256 mismatch: expected=${SHA256} actual=${ACTUAL}" >&2
  exit 1
fi
mkdir -p "${DEST}"
tar -xzf "${TMP_DIR}/${ARCHIVE}" -C "${DEST}" --strip-components=1
echo "Installed ONNX Runtime ${VERSION} for ${PLATFORM}: ${DEST}"
