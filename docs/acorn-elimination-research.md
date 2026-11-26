<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Acorn Elimination & JavaScript Provider API v2 Research (#204)

## 1. Background: Why Acorn Existed

Acorn (`third_party/acorn/acorn.js`, v8.15.0, MIT) was introduced into `klookup_core` solely to implement `moduleImportParser()` inside `src/providerpackage.cpp`. Prior to this change:

1. `JsProvider::initEngine()` loaded provider entrypoints via `QJSEngine::importModule()`, which executes ES modules (`import` / `export`) directly from the filesystem with no public Qt C++ hook to intercept or restrict module specifiers.
2. To prevent unapproved or escaping module edges (e.g., `import 'file:///etc/passwd'`, `import('../secret.mjs')`, or `import('./helper')` hidden behind strings/regexes/template literals, as well as dynamic `import(...)`), `writeVerifiedSnapshot()` embedded Acorn in a temporary `QJSEngine`, parsed each reachable module's AST strictly as data before snapshot creation, and walked static edges inside `m_files`.
3. Packaging Acorn required shipping ~200 KiB of vendored JS, carrying a local property-name mutation adapter in `src/providerpackage.cpp` to work around Qt 6.11 `parsePropertyName` hoisting warnings (#142, #160), and adding `-DKLOOKUP_USE_SYSTEM_ACORN=ON` distro wiring (#167).

## 2. Empirical Audit of Shipped & Fixture Providers

Inspection of `providers.d/` confirms that **zero** shipped JavaScript providers use ES module `import` statements (`anilist`, `cheapshark`, `lyrics_ovh`, `openlibrary`, `tvmaze` are all single-file self-contained scripts that previously only used `export` at the bottom to expose `search`, `getPage`, `getDetails`, `getContent`, and `selfTest`). Multi-file capability (`host.require('./helper.js')`) remains lightweight and desirable for helper sharing and dotfiles-managed providers (#205).

## 3. Empirical Qt 6.11.2 `QJSEngine::evaluate()` Verification

Tested directly on Debian Forky / Qt 6.11.2 (`Qt6::Qml`) in Script mode (`QJSEngine::evaluate()` without `importModule()`):

| Construct evaluated via `QJSEngine::evaluate()` | Qt 6.11.2 Result | Code Executed? |
| --- | --- | --- |
| `import x from './a.js';` | `SyntaxError: Unexpected token \`import'` | No |
| `export function search() { return []; }` | `SyntaxError: Unexpected token \`export'` | No |
| `var x = import('./a.js');` | `SyntaxError: Unexpected token \`import'` | No |
| `var p = import('file:///etc/passwd');` | `SyntaxError: Unexpected token \`import'` | No |
| `(function(){'use strict'; return import('file:///etc/passwd');})()` | `SyntaxError: Expected token \`;'` | No |
| `(Function("return import('file:///etc/passwd')"))()` | `SyntaxError: Parse error` | No |
| `eval("import('file:///etc/passwd')")` | `SyntaxError: Unexpected token \`import'` | No |
| `import.meta` | `SyntaxError: Expected token \`;'` | No |
| Built-in `require`, `XMLHttpRequest`, `fetch`, `Qt` | All `undefined` | N/A |

Because `QJSEngine::evaluate()` parses inputs under ECMAScript's **Script** goal (not Module goal) and Qt 6.11's QV4 parser rejects `ImportExpression` / `import.meta` / `ImportDeclaration` / `ExportDeclaration` at parse time, module loading via ES syntax is **structurally unavailable** without any third-party AST scanner.

Additionally, `qtqmljsparser_p.h` (`QQmlJS::Parser`) is not even shipped in the default Qt6 QML development headers (`qt6-declarative-dev`) without `-private-dev` headers.

## 4. Comparative Evaluation of Alternatives

| Option | Security Boundary | Packaging & Deps | Multi-file Support | Qt Compatibility | Decision |
| --- | --- | --- | --- | --- | --- |
| **1. `QJSEngine::evaluate()` Classic Script + KLookup Provider API v2 (`module.exports`, in-memory `host.require`)** | **Equivalent / Stronger:** ESM `import`/`export`/dynamic `import()` rejected structurally at parse time; no temp dir on disk; `verifiedFiles()` re-checks `approvalFingerprint()` before execution; `host.require()` confined strictly to immutable in-memory `m_files`. | **Zero extra deps**, removes Acorn completely. | Yes, bounded in-memory `host.require('./...')` with cycle detection and traversal blocking. | Public `Qt6::Qml` API only (`QJSEngine::evaluate`), portable across Qt 6.x. | **Selected** |
| **2. Qt Private `QQmlJS::Parser`** | Requires parsing AST in C++. | Needs private Qt headers (`Qt6QmlPrivate`), brittle across minor Qt releases. | Possible, with maintenance burden. | Private ABI breaks on Qt upgrades. | Rejected |
| **3. System / Vendored Acorn (`dev-nodejs/acorn`)** | AST walk before `importModule()`. | Requires vendored JS (~200 KiB) or distro Node package; Qt 6.11 hoisting issues. | Native ESM | Needed Qt 6.11 `parsePropertyName` shim. | Eliminated |
| **4. QML `QQmlEngine` runtime** | **Wider attack surface:** QML `import QtQuick`, `XMLHttpRequest`, `Qt.*` globals, UI object creation; QML is NOT a sandbox. | Pulls QtQuick/GUI dependencies into headless runners. | QML component imports | Adds Quick deps. | Rejected for core JS runtime (evaluated separately in #206) |
| **5. Regex / heuristic JS scanner** | **Unsafe:** bypassable via string/regex/template-literal quirks. | Tiny | Fragile | N/A | Rejected |

## 5. Security Boundary Proof (API v2)

1. **Zero execution during discovery:** `ProviderPackageDiscovery` / `ProviderPackage::fromDirectory()` only read `provider.json` and bounded asset bytes into `m_files`, computing the SHA-256 `contentHash()` and `approvalFingerprint()`. Zero JS parsing or evaluation happens prior to explicit user approval and search invocation.
2. **Pre-execution integrity check:** `JsProvider::initEngine()` calls `ProviderPackage::verifiedFiles()`, which re-reads the directory on disk and aborts if `approvalFingerprint()` changed (detecting tampered `main.mjs`, helpers, or permissions). No temporary directory snapshot is written to disk, eliminating TOCTOU windows between snapshot write and `QJSEngine::importModule()`.
3. **Structural exclusion of external modules:** Scripts are wrapped in `(function(host, module, exports) { "use strict";\n<source>\n})` and evaluated in memory via `QJSEngine::evaluate()`. No filesystem loader exists on `QJSEngine`.
4. **Frozen minimal capability bridge:** `host` exposes only `apiVersion` (`2`), `fetch` (enforcing manifest `origins`, HTTPS, IP/redirect checks, request limits, and host-side KWallet credential injection), `getSecret`, `log`, and `require(spec)` (confined strictly to `m_verifiedFiles` with `./`/`../` traversal and cycle guards). Internal helpers (`__klookupBridge`) are deleted from global scope after bootstrap.
5. **CPU and cancellation budgets preserved:** `JsExecutionWatchdog`, `JsInterruptionGate`, per-operation `OperationPromise` accounting, and cancellation semantics remain unchanged.

## 6. Migration Guide: Experimental API v1 (ESM) → API v2 (Classic Script)

KLookup's provider scripting format is pre-1.0 experimental. Shipped providers and test fixtures are migrated in this change:

Before (ESM v1):
```js
import { helper } from "./helper.mjs";
export function search(query) { return []; }
export { getDetails };
```

After (Provider API v2):
```js
const { helper } = host.require("./helper.mjs"); // optional for multi-file packages
function search(query) { return []; }
module.exports = { search, getDetails };
```

- Single-file providers remain self-contained and require no `host.require()`.
- Existing `host.fetch()`, `host.log()`, Promise return values, result/detail JSON shapes, `diagnostics.json` + `selfTest()`, and `provider.json` permission declarations remain unchanged.
- Attempting to load an unmigrated `import`/`export` script fails safely with a clear diagnostic error directing authors to `module.exports` and `host.require`.
