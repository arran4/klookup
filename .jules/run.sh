#!/bin/bash
set -euo pipefail

CACHE_DIR="${HOME}/.cache/kde-dev-rootfs"

if [ ! -f "${CACHE_DIR}/.ready" ]; then
    echo "Rootfs not ready. Please run .jules/bootstrap.sh first."
else

    mount_proc=false
    mount_sys=false
    mount_dev=false
    mount_workspace=false

    function cleanup() {
        set +e
        rm -f run_inner.sh
        if [ "$mount_workspace" = true ]; then sudo umount "${CACHE_DIR}/workspace" 2>/dev/null; fi
        if [ "$mount_dev" = true ]; then sudo umount -R "${CACHE_DIR}/dev" 2>/dev/null; fi
        if [ "$mount_sys" = true ]; then sudo umount "${CACHE_DIR}/sys" 2>/dev/null; fi
        if [ "$mount_proc" = true ]; then sudo umount "${CACHE_DIR}/proc" 2>/dev/null; fi
        sudo rm -f "${CACHE_DIR}/etc/resolv.conf"
        sudo mv "${CACHE_DIR}/etc/resolv.conf.bak" "${CACHE_DIR}/etc/resolv.conf" 2>/dev/null
    }

    trap cleanup EXIT

    sudo mount -t proc proc "${CACHE_DIR}/proc" && mount_proc=true
    sudo mount -t sysfs sys "${CACHE_DIR}/sys" && mount_sys=true
    sudo mount --rbind /dev "${CACHE_DIR}/dev" && mount_dev=true

    if [ -f "${CACHE_DIR}/etc/resolv.conf" ]; then
        sudo mv "${CACHE_DIR}/etc/resolv.conf" "${CACHE_DIR}/etc/resolv.conf.bak"
    fi
    sudo cp /etc/resolv.conf "${CACHE_DIR}/etc/resolv.conf"

    sudo mkdir -p "${CACHE_DIR}/workspace"
    sudo mount -o bind "$(pwd)" "${CACHE_DIR}/workspace" && mount_workspace=true

    cat << 'INNER_EOF' > run_inner.sh
#!/bin/bash
set -euo pipefail
export QT_QPA_PLATFORM=offscreen
cd /workspace
exec "$@"
INNER_EOF
    chmod +x run_inner.sh

    sudo chroot "${CACHE_DIR}" /bin/bash -c "cd /workspace && ./run_inner.sh \"\$@\"" _ "$@"

fi
