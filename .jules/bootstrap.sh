#!/bin/bash
set -euo pipefail

ROOTFS_URL="https://github.com/arran4/kde-dev-rootfs/releases/latest/download/kde-dev-rootfs-forky-amd64.tar.zst"
SHA_URL="${ROOTFS_URL}.sha256"
CACHE_DIR="${HOME}/.cache/kde-dev-rootfs"
ARCHIVE_PATH="${HOME}/.cache/kde-dev-rootfs-forky-amd64.tar.zst"
MARKER_FILE="${CACHE_DIR}/.ready"

if [ -f "${MARKER_FILE}" ]; then
    echo "Rootfs is already bootstrapped at ${CACHE_DIR}"
else
    mkdir -p "${HOME}/.cache"

    echo "Downloading rootfs..."
    curl --fail --location --retry 3 -o "${ARCHIVE_PATH}" "${ROOTFS_URL}"
    curl --fail --location --retry 3 -o "${ARCHIVE_PATH}.sha256" "${SHA_URL}"

    echo "Verifying checksum..."
    cd "${HOME}/.cache"
    sha256sum -c <(awk '{print $1"  kde-dev-rootfs-forky-amd64.tar.zst"}' "kde-dev-rootfs-forky-amd64.tar.zst.sha256")

    echo "Extracting rootfs..."
    sudo mkdir -p "${CACHE_DIR}"
    sudo tar --zstd -xf "kde-dev-rootfs-forky-amd64.tar.zst" -C "${CACHE_DIR}" --numeric-owner

    sudo touch "${MARKER_FILE}"
    echo "Bootstrap complete."
fi
