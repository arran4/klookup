# KLookup icon sources

The open book and magnifying glass form an original KLookup identity. The blue /
turquoise tile and white book provide a consistent silhouette across light and
dark desktops. The monochrome variant uses KDE's `ColorScheme-Text` class and
`currentColor` so symbolic-icon consumers can recolor it.

All SVG sources and generated PNGs in this directory are dedicated to the public
domain under **CC0-1.0**. Created for KLookup in 2026. No third-party artwork,
trademarks, fonts or generated photo assets are included. License:
https://creativecommons.org/publicdomain/zero/1.0/legalcode

Edit `scalable/apps/org.kde.klookup.svg`, then regenerate each PNG using librsvg:

```sh
for size in 16 22 24 32 48 64 128 256; do
    mkdir -p "icons/${size}x${size}/apps"
    rsvg-convert -w "$size" -h "$size" \
        icons/scalable/apps/org.kde.klookup.svg \
        -o "icons/${size}x${size}/apps/org.kde.klookup.png"
done
```

CMake installs the set into hicolor. The app embeds the scalable primary and a
256px PNG fallback, so running from the build tree needs no theme installation.
The stable icon name is `org.kde.klookup` in windows, launcher, tray, notifications
and KRunner. The desktop-file identity is the same on Wayland.

Actions use standard themed icons: `edit-find`, `internet-web-browser`,
`list-add` (load more), `window-new`, `document-open-recent`, `edit-clear`,
`window-close`, `configure`, and `dialog-password`. Existing KDE standard actions
keep their theme icons; future feature actions should use these names rather than
bundling replacement system artwork. No AppStream file currently exists.

Automated verification checks installed paths/dimensions and embedded-icon
rendering at small/HiDPI sizes. Final light/dark tray/taskbar appearance and
Wayland desktop identity should also be inspected in a Plasma session.

The generated Qt resource object targets explicitly enable position-independent
code because the static core and its resources are linked into provider modules.
This also supports address/undefined sanitizer builds without relocation errors.
