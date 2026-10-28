# Recent activity

The dashboard shows up to ten latest searched queries, viewed results and captured
clipboard items, each labelled by event/source and timestamp. The store retains at
most 50 entries for 30 days; clipboard entries expire after 24 hours. Equivalent
events move to the front without conflating a capture with a search or a viewed
result. Query text is retained exactly for revisiting, independently of its bounded
one-line display label. Viewed results use stable provider/result IDs and a safe
source URL, with no provider instance or tab pointer in history.

Enter activates the selected item, Delete removes it, and explicit buttons offer
the same actions. Viewed entries open their validated source externally; queries
run a deliberate search. Entries without a source can search the title. Clear
recent activity confirms its scope and deletes the saved activity group; it keeps
open tabs and the existing 50-item manual autocomplete history. Hiding the section
only changes visibility and does not delete saved history. Session restoration is
not a new search event, so cleared activity does not reappear just because query
tabs restore.

Settings → General has three independent controls:

- Show recent activity (on by default).
- Record clipboard activity (off by default). Enabling this alone retains clipboard
  captures/automatic searches only in memory. Turning it off removes these entries.
- Save recorded clipboard activity between sessions (off by default, requires
  recording enabled). Explicitly permits KConfig persistence for up to 24 hours.

Capture eligibility and URL/content classification from #49/#56 still run first.
Automatic clipboard searches keep their origin through the search path; they
cannot become ordinary persisted query activity. Their query tabs are session-only
as well, preventing tab restoration from bypassing clipboard privacy preferences.
Deliberately typed or revisited queries retain normal tab/completer behavior.
These settings do not redact arbitrary text deliberately searched by the user;
credentials from provider authentication are never passed into this store.

Stored fields are bounded public history: title/query/snippet, event kind, source
and stable ID, canonical URL and timestamp. Credential-bearing source URLs and
URLs with common token/password/authorization query or fragment parameters are rejected.
Malformed/oversized records and future/expired timestamps are ignored. Clear and
privacy opt-out remove disallowed persisted items immediately. A minute timer
expires entries during long sessions without focus changes or notifications.

This PR is a third-level sibling on clipboard-policy PR #62. It uses only the
existing plain SearchResult fields and stable provider IDs, requiring no metadata
PR parent. The result-view signal must be preserved when combining #58/#59's
SearchTab: emit after explicit row selection/rendering, not when a late detail
response refreshes the view. New windows in #52 should share one application-level
RecentActivity store instead of independent writers.

Legacy saved tabs have no input-origin marker; their original source is unavailable.
Provider snippets in recent-item tooltips are explicitly escaped, preventing Qt
rich-text autodetection from loading markup resources.
