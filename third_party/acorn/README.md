<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

Acorn 8.15.0, `dist/acorn.js` from the official npm release:
https://registry.npmjs.org/acorn/-/acorn-8.15.0.tgz

Upstream: https://github.com/acornjs/acorn

The download was verified against the release SHA-512 integrity:
sha512-NZyJarBfL7nWwIq+FDL6Zp/yHEhePMNnnJ0y3qfieCrmNvYct8uvtiV41UvlSe6apAfk0fY1FbWx+NwfmpvtTg==

This MIT-licensed parser validates module imports before QJSEngine loads a
verified package snapshot. Provider code is parsed as data, never evaluated by
the parser. Node/npm is not a build or runtime dependency.

Local compatibility change for Qt 6.11 (#142): the single declaration of
`FUNC_STATEMENT`, `FUNC_HANGING_STATEMENT` and `FUNC_NULLABLE_ID` is moved before
the statement-parser methods that reference them. Values and parser behavior are
unchanged. Qt's static declaration-order analysis otherwise emits two
`qt.qml.usedbeforedeclared` warnings each time the parser is evaluated, even
though JavaScript hoisting makes the upstream order legal. The upstream archive
integrity above describes the original download, before this documented change.
No logging category is suppressed. A snapshot/import-parser regression captures
and asserts absence of this warning while validating module imports and function
syntax; installed package smoke tests use the same embedded resource.
