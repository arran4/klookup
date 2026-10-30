<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

Acorn 8.15.0, unmodified `dist/acorn.js` from the official npm release:
https://registry.npmjs.org/acorn/-/acorn-8.15.0.tgz

Upstream: https://github.com/acornjs/acorn

The download was verified against the release SHA-512 integrity:
sha512-NZyJarBfL7nWwIq+FDL6Zp/yHEhePMNnnJ0y3qfieCrmNvYct8uvtiV41UvlSe6apAfk0fY1FbWx+NwfmpvtTg==

This MIT-licensed parser validates module imports before QJSEngine loads a
verified package snapshot. Provider code is parsed as data, never evaluated by
the parser. Node/npm is not a build or runtime dependency.
