<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Provider HTTP transport

The shared host accepts GET and JSON/text POST requests only after validating
package trust and declared full origins (scheme, hostname and effective port).
Request IDs identify independent cancellable tickets, including pending secret
resolution and queued failures. Reusing an ID cancels the old ticket; a late
callback cannot revive it. Changing the package approval identity or revoking
trust cancels outstanding requests.

Every ticket has one absolute deadline, including redirects and pending host
credential resolution. The default is 10 seconds, capped at 30 seconds. Cancel
and destruction abort replies and suppress pending completion. Replies are
retired with deleteLater; completion is delivered once, asynchronously, so host
consumers can safely destroy their objects.

Both compressed/wire payload and decoded payload have independent hard 5 MiB
limits. Exactly 5 MiB is accepted; the next byte aborts and clears the partial
body. Reads use at most 32 KiB and Qt's reply read buffer is limited to 64 KiB.
Content-Length is an early rejection hint, not the enforcement mechanism. The
same drain runs at readyRead and finished, so empty readyRead after an exact
limit and data first available at completion are handled correctly.

The host explicitly advertises gzip and deflate. This disables Qt's transparent
decompressor and allows the host to count the original bytes while decoding
incrementally with zlib into a separately bounded buffer. Gzip concatenated
members are supported; malformed/truncated streams and unsupported encodings
fail explicitly. This adds a zlib development dependency (`zlib1g-dev` in the
shared Debian rootfs), included in CI's dependency installation.

Redirects are manual, limited to five hops and recheck full origin permissions.
HTTPS downgrade is forbidden. On changed scheme/host/effective port, secret
injection and all headers except non-secret Accept, Content-Type and User-Agent
are removed; an injected header is removed even if its name is in that set.
Automatic cookie load/save and authentication reuse are disabled, so a shared
cookie jar cannot silently recreate a removed header on the new origin.
Transport-controlled headers and URL user-info cannot bypass origin checks.
POST follows the usual 301/302/303 conversion to GET; 307/308 preserve the method.

A completed HTTP 4xx/5xx response retains its bounded body, actual status/reason
and headers (including Retry-After). Host success means completed transport;
JavaScript response.ok still reflects 2xx status. Transport failures reject
without a partial body. Providers can report rate limits without aggressive
automatic retries. This does not provision or complete production scripted
KWallet credentials under issue #85.

The deterministic tests use real QTcpServer sockets for Qt framing, compressed
and decompressed limits, small/slow chunks, misleading lengths, redirects,
changed-port header/cookie isolation, timeout, concurrent IDs and destruction.
Mock replies additionally exercise exact-limit empty readyRead and completion-
time data that real Qt normally signals earlier. Ordinary CI contacts no upstream
provider endpoint.

Qt documents the behavior of downloadProgress and its transformations at
https://doc.qt.io/qt-6/qnetworkreply.html#downloadProgress. The explicit
Accept-Encoding/decompression behavior is verified against Qt's own source:
https://github.com/qt/qtbase/blob/v6.11.2/src/network/access/qnetworkreplyhttpimpl.cpp.
