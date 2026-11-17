<!-- SPDX-FileCopyrightText: 2026 KLookup contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Novel Updates series search

The `novelupdates` scripted provider searches Japanese, Korean, Chinese and other
Asian light/web novel series at [Novel Updates](https://www.novelupdates.com/).
It returns series titles and links as book-type entries; it does **not** fetch
novel text, bypass a login, follow translation chapter redirects or access
private reading lists.

## Integration and limitations

Novel Updates does not publish a supported public search API. This provider
uses the site's desktop **live search HTML** response from
`POST https://www.novelupdates.com/wp-admin/admin-ajax.php`, using
`action=nd_ajaxsearchmain`, `strType=desktop`, `strOne=<query>` and
`strSearchType=series`. It recognizes `search_li_results` and `a_search`
markup, accepts only canonical Novel Updates `/series/<slug>/` links, and
returns the title and attribution. No results pagination or series-details
fetch is implemented; metadata available on individual series pages is not
claimed to be part of search results.

Because this is an internal, undocumented HTML endpoint, it can change or
become unavailable at any time. Anti-bot challenges and HTTP 403 are surfaced
as errors; KLookup makes no attempt to solve CAPTCHAs, simulate browser cookies
or circumvent access restrictions. Site availability has **not** been verified
from the development environment. Live testing is required before considering
the integration production-ready.

Requests occur only when the user searches an enabled provider. Each request
uses the shared transport's at-least-2.5-second per-origin spacing,
a short bounded in-memory result cache and HTTP 429 Retry-After handling. No
background crawling, harvesting, login or automatic retry loops are performed.
Only `https://www.novelupdates.com` is granted in the manifest. Queries are
sent to that third-party site and should not contain private information.

## Testing

`diagnostics.json` and `selfTest` exercise synthetic search responses,
Unicode/entity decoding, empty results, invalid/foreign links and bot
challenges without network access. The Qt network fixture tests form encoding,
result mapping, HTTP failures and cooldowns.

Run `ctest --test-dir build -R 'novelupdates' --output-on-failure` after building.
Confirm the real search flow with a user-driven search in a desktop session
and verify whether Novel Updates permits application-originated requests.
