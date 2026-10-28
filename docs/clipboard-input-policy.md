# Automatic clipboard input policy

`ClipboardInput` classifies content before it can prepopulate the dashboard,
trigger an automatic query or enter captured activity. Eligibility is independent
of the visibility/trigger policy tracked in #49. This classification makes no
network or DNS requests and never stores rejected text in its result.

- Accept only explicit `text/plain` MIME data. HTML-only, URI-list-only, image and
  binary formats are ignored. A rich clipboard with a plain-text alternative uses
  only that plain text.
- Trim whitespace; ignore empty text. At most 4,096 UTF-16 code units are accepted.
  NUL, binary control characters and replacement characters are unsupported;
  tabs/newlines and normal Unicode lookup text are allowed.
- Reject pure explicit web URLs and recognizable URI schemes, including file,
  mailto, tel, magnet, urn, data and javascript. Custom `scheme://` URIs are also
  rejected. URI validation is conservative: malformed `https://` is still excluded.
- Exclude bare `www.` addresses, common domain suffixes (listed in the classifier),
  domains with explicit paths, bracketed IPv6 addresses, and localhost with a port. Unknown bare suffixes
  are deliberately not treated as URLs until they have a `www.` prefix or path.
  This avoids confusing `QtCore.QString`, `Dr.Smith`, `readme.md`, ordinary book
  identifiers and dotted versions with browsing data. Bare IPv4-like versions
  remain text unless accompanied by a port/path or an explicit scheme.
- Bare email addresses and recognizable local filesystem paths are unsupported.
  Ordinary prose containing a link, email, punctuation or multiline text remains
  eligible; a multiline list containing only URLs is excluded. This is a content classifier, not a general secret detector.

The public `processClipboardText` path is passive clipboard capture and applies
this policy even when called directly. Explicit searches through the dashboard,
D-Bus or `performSearch` are separate: deliberately searching a URL is allowed.
Future explicit clipboard shortcuts must use that explicit-search path according
to their documented policy. Automatic captures never write search autocomplete
history; recent-capture retention is separately controlled by #50.

Copying a result's HTTP(S) URL cannot bounce back into an automatic query because
it is rejected before capture and deduplication. Existing consecutive automatic
query deduplication is preserved after whitespace normalization.
