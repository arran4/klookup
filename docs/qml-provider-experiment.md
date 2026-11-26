<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Experimental QML Provider Presentation Study (#206)

This document records an architectural and security evaluation of QML-based presentation for KLookup providers, anchored around a synthetic MangaBaka prototype addressing issues [#135](https://github.com/arran4/klookup/issues/135), [#144](https://github.com/arran4/klookup/issues/144), and [#146](https://github.com/arran4/klookup/issues/146).

## Build Flag and Packaging Isolation

The prototype is gated behind an opt-in CMake option that defaults to **OFF**:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON -DKLOOKUP_BUILD_QML_EXPERIMENT=ON
```

- When `KLOOKUP_BUILD_QML_EXPERIMENT=OFF` (default), KLookup does **not** look up `Qt6Quick` or `Qt6QuickWidgets` and adds **zero** runtime QML UI dependencies to standard builds or Gentoo ebuilds.
- `test_qml_mangabaka_experiment` validates `MangaBakaViewModel`, `MangaBakaDetailView.qml`, `MangaBakaFullProvider.qml`, and the `QTextBrowser` HTML fallback under `QT_QPA_PLATFORM=offscreen`.

## Architectural Comparison (9 Axes)

| Axis | Approach A: C++/JS Logic + QML View (Host-Controlled) | Approach B: QML-Hosted Logic + View | Approach C: Current C++/JS Providers + QtWidgets/HTML Renderer |
|---|---|---|---|
| **1. Architecture & data flow** | Providers emit normalized `SearchResult` / `ResultMetadata` / `ResultContent`; host populates typed Qt property models (`MangaBakaViewModel`) bound by QML views. | QML component performs parsing/normalization and renders UI directly from JSON or network calls. | Native C++ and sandboxed `QJSEngine` providers emit normalized data rendered via `QTreeWidget` + `QTextBrowser`. |
| **2. Security & sandboxing** | Strongest if QML views are restricted to vetted built-ins or strict template selection; avoids giving untrusted JS scripts full QML runtime access. | **Weakest / un-sandboxed:** QML executes imperative JS, can instantiate arbitrary `QtQuick`/`Image`/`Loader` elements, trigger unrestricted network requests, and bypass `ProviderHostService`. **Not safe for third-party/user packages.** | Strongest isolation: user JS runs in bare `QJSEngine` (`host.fetch()` allowlisted origins only), with no filesystem, QML item tree, or ambient network access. |
| **3. Performance & startup** | Modest overhead only when detail pane opens (`QQuickWidget` init on demand); main list stays fast `QTreeWidget`. | Higher memory/startup cost per provider if each loads its own QML engine/component tree. | Fastest startup, lowest memory footprint (~0 QML scene-graph overhead). |
| **4. Visual fidelity & MangaBaka issues (#135, #144, #146)** | Excels at badge chips, responsive flows, multi-language title expanders (#144), and dense tag/rating cards (#135/#146). | Same visual flexibility as A, at the cost of mixing logic and layout. | More constrained by `QTextBrowser` CSS subset and tree columns, though predictable and uniform. |
| **5. Maintainability & testability** | C++ model unit-tested without GUI; QML view tested independently with synthetic fixtures; clear separation of concerns. | Business logic coupled to QML scene lifecycle; harder to run fast headless logic tests or static checks. | Well-established, heavily tested in C++ CTester suites (`test_provider_mangabaka`, `test_mangabaka_list_titles`, `test_result_ui_and_errors`). |
| **6. Desktop integration & accessibility** | Requires careful palette/Kirigami styling (`SystemPalette`) and explicit screen-reader `Accessible` bindings inside `QQuickWidget`; focus traversal across widget/QQuick boundaries needs extra care. | Same accessibility and `QQuickWidget` focus-proxy hurdles, duplicated per provider. | Native QtWidgets/KF6 theme, fonts, DPI, keyboard shortcuts, context menus, and Orca/AT-SPI accessibility work out of the box. |
| **7. Build & Gentoo packaging impact** | Requires `Qt6Quick` and `Qt6QuickWidgets` (`dev-qt/qtdeclarative[widgets]`) at build & runtime (`qml6-module-*`). | Same Qt6 dependencies plus harder third-party runtime auditing. | Zero extra dependencies (`Qt6::Widgets` + `Qt6::Qml` core JS engine only). |
| **8. Fallback behaviour** | Clean fallback: `QmlHostWidget` falls back to normalized `QTextBrowser` HTML if QML scene creation or OpenGL/Vulkan fails. | Hard to fall back cleanly when provider search/mapping logic lives inside the QML file itself. | Always uses reliable `QTextBrowser` / `QTreeWidget`. |
| **9. Third-party script provider compatibility** | Works with scripted API v1/v2 providers via normalized metadata schemas without exposing QML to untrusted packages. | **Incompatible** with KLookup's privilege-separated scripted provider model (#85/#168/#204). | 100% compatible with approved scripted provider packages. |

## Security Warning: QML Is Not a Sandbox for Untrusted Code

A critical finding of this study is that **QML (`QQmlEngine` / `QQuickWidget`) must never be treated as a security sandbox** for untrusted or third-party provider packages:

1. **Ambient element capabilities:** QML `Image`, `MediaPlayer`, `Loader`, or `XMLHttpRequest` can initiate network requests outside `ProviderHostService`'s origin allowlist unless an invasive `QQmlNetworkAccessManagerFactory` and custom `QQmlAbstractUrlInterceptor` lock everything down.
2. **Import surface:** Unless `QML_IMPORT_PATH` and custom `QQmlEngine` import filtering are hardened, QML files can import system QtQuick modules (`Qt.labs.folderlistmodel`, `QtQuick.Dialogs`, etc.).
3. **Executable JS inside bindings:** Every QML property binding is arbitrary ECMAScript evaluated on the GUI thread.

Consequently, third-party and dotfiles-managed JS providers should **remain restricted to pure data transforms in `QJSEngine` (`apiVersion: 2`)**, emitting structured JSON/metadata for host-controlled presentation.

## Recommendation

Keep **Approach C (QtWidgets/KF6)** as the default lightweight baseline while selectively adopting **Approach A (host-owned QML/declarative detail templates fed by C++ models)** *only if* rich visual cards cannot be cleanly achieved with QtWidgets/KF6 layouts—never **Approach B** for untrusted providers. Keep PR #206 **Draft** and experimental until the maintainer decides whether any built-in views should adopt optional QML presentation.
