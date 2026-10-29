<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Script operation lifecycle

Each exported search, page, details or content call has its own operation record,
wall deadline, handler, network requests and internal generation token. The wall
deadline defaults to 10 seconds and is independent of HTTP timeouts. The host can
set a shorter deadline, up to a hard maximum of 30 seconds. A promise that never
settles fails once with `Timeout`; subsequent operations can still run.

The ECMAScript promise adapter carries the operation token on both promises
(through a host-owned registry) and registered callbacks. This includes promise assimilation,
multiple/chained fetches and overlapping operations. The public request ID can
be reused without reviving an older operation. The private bridge and accounting
helpers are not exposed through `host`; scripts see fetch, log and getSecret.
Promise roots are released with their owning operation, including on cancellation.
Module state remains per provider instance, including caches across operations.
Host fetches require an active exported operation, preventing detached module
initializers from starting unowned HTTP work.

Completion, cancellation, timeout, reload and destruction drop deferreds,
handlers and request maps and abort all HTTP work still owned by the operation.
Cancellation is silent; deadline and other failures use the corresponding
terminal signal. Terminal delivery is queued until JavaScript has returned, so a
consumer can synchronously close its tab or destroy its provider in the signal
handler. Cancelling before that queued delivery suppresses it as well.

A dedicated watchdog thread calls only QJSEngine::setInterrupted(), which Qt
explicitly documents as thread-safe. Initialization has a 250 ms execution
budget; each operation has a cumulative 100 ms budget across its JavaScript
calls and promise callbacks. Waiting for HTTP is excluded. Interrupted execution
invalidates the engine and fails all its active operations once; Retry/reload
creates a fresh engine. Disposal runs after the interrupted JavaScript stack
unwinds, before pending promise jobs. A public application event filter also
handles jobs already present in Qt's current posted-event batch; other engines'
events are preserved. This avoids Qt
6.11's unsafe thenable-adoption exception path when interruption remains set,
and destroys the engine's queued reactions. User thenables are adopted under
the originating operation's execution guard. The watchdog is stopped and joined
before destroying the engine. Ordinary promise wall timeouts do not invalidate the engine.

This is trusted in-process QJSEngine execution, not an OS sandbox. The execution
budget bounds JavaScript loops and callbacks through Qt's interruption mechanism;
it cannot preempt arbitrary blocking native code or provide an OS memory limit.
Only explicitly approved code (or protected distribution packages under the
approval policy) may run. Qt's supported ECMAScript environment is documented at
https://doc.qt.io/qt-6/qtqml-javascript-hostenvironment.html and its interruption
API at https://doc.qt.io/qt-6/qjsengine.html#setInterrupted.

A provider owns its host and transport by default. Direct clones own independent
hosts and network managers, and application factories give each tab the same
ownership hierarchy. An explicitly injected host/transport is borrowed and held
through QPointer; destroying it first is safe. Pending credential callbacks use a
weak host reference and generation check, so cancellation or destruction cannot
start HTTP later. This ownership guard does not implement the separate production
KWallet provisioning, restore/revoke and clone-context acceptance in issue #85.

On Linux these are per-thread CPU budgets, measured from the engine thread's
POSIX CPU clock by both accounting and watchdog polling (at most 5 ms apart).
Scheduler delays and blocking native waits do not spend the CPU allowance; wall
operation deadlines remain separate. Platforms without this clock retain an
elapsed-execution fallback. This does not add native-code preemption or an OS
sandbox. The unchanged busy-loop/job-storm tests still enforce interruption, and
a native-wait regression proves waiting longer than the CPU allowance alone does
not invalidate a healthy engine on Linux.
