<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Third-Party and Vendored Dependencies Policy

KLookup minimizes vendored code and prohibits implicit runtime or build-time downloading of executable third-party packages. Any temporary vendored dependency must have documented provenance, license compatibility, integrity verification, and an explicit elimination/system-packaging path.

## Current Vendored Inventory

Only one third-party component exists in `third_party/`:

- **Component:** Acorn (`third_party/acorn/acorn.js`)
- **Version:** `8.15.0` (UMD dist build)
- **Upstream:** <https://github.com/acornjs/acorn>
- **Upstream Archive:** <https://registry.npmjs.org/acorn/-/acorn-8.15.0.tgz>
- **Upstream Archive SHA-512:** `sha512-NZyJarBfL7nWwIq+FDL6Zp/yHEhePMNnnJ0y3qfieCrmNvYct8uvtiV41UvlSe6apAfk0fY1FbWx+NwfmpvtTg==`
- **Patched In-Tree SHA-256:** `16bcf7923b6c8ddfa536e09cd407b1d0068fe20efbdad5330a50f3280923e414`
- **License:** `MIT` (attributed in `third_party/acorn/LICENSE`, `third_party/acorn/acorn.js.license`, and `LICENSES/MIT.txt`; compatible with KLookup's `GPL-3.0-or-later` license)
- **Local Modifications:** Declaration order of `FUNC_STATEMENT`, `FUNC_HANGING_STATEMENT`, and `FUNC_NULLABLE_ID` moved prior to references to eliminate Qt 6.11 `qt.qml.usedbeforedeclared` warnings without altering runtime behavior (#142, #160).

## Why Pre-Execution AST Parsing Is Required

In `src/providerpackage.cpp` (`moduleImportParser` and `writeVerifiedSnapshot`), KLookup validates JavaScript provider modules **before** loading or executing them in `QJSEngine`:

1. Provider source code is parsed purely as data into an AST (`acorn.parse(source, {ecmaVersion: 2020, sourceType: 'module'})`).
2. Static `ImportDeclaration`, `ExportNamedDeclaration`, and `ExportAllDeclaration` edges are extracted and verified to stay within the verified package directory without path traversal or escaping schemes.
3. Dynamic `import(...)` (`ImportExpression`) and invalid syntax are rejected before execution.

### Why Qt / Regex Alternatives Are Insufficient

- **`QJSEngine::importModule()`:** Evaluates module top-level code immediately upon import. It cannot safely inspect an untrusted module's import graph or syntax without executing potentially malicious code.
- **Qt Public API:** Qt 6 (`Qt6::Qml`) does not expose a public, stable C++ ECMAScript AST parser API.
- **Regex / Heuristic Scanning:** Regular expressions and naive tokenizers fail on edge cases such as imports hidden inside comments, string literals, regex literals, or template literal substitutions, and can cause false positives or security bypasses.

## Using System-Installed Acorn (`KLOOKUP_USE_SYSTEM_ACORN`)

Distributions and packagers (such as Gentoo or Debian) that ship Acorn as a system package can eliminate the bundled copy at build time:

```bash
cmake -S . -B build -DKLOOKUP_USE_SYSTEM_ACORN=ON
```

CMake searches standard distribution locations (`/usr/share/nodejs/acorn/dist`, `/usr/share/javascript/acorn`, `/usr/lib/node_modules/acorn/dist`, `/usr/local/lib/node_modules/acorn/dist`). A custom path can also be supplied explicitly:

```bash
cmake -S . -B build -DKLOOKUP_USE_SYSTEM_ACORN=ON -DKLOOKUP_ACORN_PATH=/path/to/acorn.js
```

Note: When using an unpatched upstream Acorn release under Qt 6.11+, Qt's runtime adapter in `src/providerpackage.cpp` preserves `parsePropertyName` semantics, while `qt.qml.usedbeforedeclared` warnings may appear for upstream hoisting order.

## Long-Term Elimination Roadmap

To eventually eliminate the vendored fallback completely:

1. **System Packaging:** Downstream packages (e.g. Gentoo ebuilds) should enable `-DKLOOKUP_USE_SYSTEM_ACORN=ON` against `dev-nodejs/acorn` (or equivalent).
2. **Optional Runtime Isolation:** Coordinate with third-party provider catalogue/runtime isolation (#181) to move the optional JavaScript provider runtime and parser into an independently installable component/plugin package rather than bundling it in the core binary.
3. **Documented Exception Policy:** Until optional runtime isolation or universal distro availability is established, the pinned, MIT-attributed `third_party/acorn/acorn.js` remains a documented, audited exception verified by REUSE and `test_acorn_parser_validation`.
