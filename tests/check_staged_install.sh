#!/bin/bash
# SPDX-FileCopyrightText: 2026 KLookup contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
build_dir="$1"
# Distribution trust requires a protected root-owned prefix. CTest runs as root
# through .jules/run.sh; local unprivileged CTest explicitly reports a skip.
if [ "$(id -u)" -ne 0 ]; then
    echo "Protected staged installation requires the .jules/run.sh rootfs"
    exit 77
fi
stage=$(mktemp -d /opt/klookup-install.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
cmake --install "$build_dir" --prefix "$stage" > "$stage/install.log"
test -f "$stage/share/klookup/providers.d/tvmaze/provider.json"
test -f "$stage/share/klookup/providers.d/tvmaze/main.mjs"
if sed -n '/tvmaze.*\.so/p' "$stage/install.log" | read -r obsolete; then
    echo "Obsolete TVMaze library is still installed"
    exit 1
fi
mkdir -p "$stage/lib/plugins/klookup" "$stage/empty-user" "$stage/config"
cp "$build_dir/tests/obsolete/provider_tvmaze.so" "$stage/lib/plugins/klookup/"
cp "$build_dir/tests/test_tvmaze_scripted" "$stage/bin/test-installed-tvmaze"
XDG_DATA_HOME="$stage/empty-user" XDG_DATA_DIRS="$stage/share" XDG_CONFIG_HOME="$stage/config" \
    KLOOKUP_STAGED_INSTALL=1 "$stage/bin/test-installed-tvmaze" testFreshInstalledDiscovery
if [ -x "$build_dir/tests/test_scripted_lyrics" ]; then
    test -f "$stage/share/klookup/providers.d/lyrics_ovh/provider.json"
    test -f "$stage/share/klookup/providers.d/lyrics_ovh/main.mjs"
    cp "$build_dir/tests/obsolete/provider_lyrics_ovh.so" "$stage/lib/plugins/klookup/"
    cp "$build_dir/tests/test_scripted_lyrics" "$stage/bin/test-installed-lyrics"
    XDG_DATA_HOME="$stage/empty-user" XDG_DATA_DIRS="$stage/share" XDG_CONFIG_HOME="$stage/config-lyrics" \
        KLOOKUP_STAGED_INSTALL=1 "$stage/bin/test-installed-lyrics" normalDiscoveryApprovalAndEnablement
fi
