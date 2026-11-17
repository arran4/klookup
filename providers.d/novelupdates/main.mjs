// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Novel Updates has no public search API. This is its public desktop live-search
// HTML endpoint, not a stable API contract. Do not bypass site access controls.
var cache = Object.create(null);
var retryAt = 0;
var endpoint = "https://www.novelupdates.com/wp-admin/admin-ajax.php";

function decodeEntities(value) {
    return value.replace(/&(#(?:x[0-9a-f]+|[0-9]+)|amp|lt|gt|quot|apos|nbsp);/gi, function(match, entity) {
        var named = {amp: "&", lt: "<", gt: ">", quot: '"', apos: "'", nbsp: " "};
        var lower = entity.toLowerCase();
        if (Object.prototype.hasOwnProperty.call(named, lower)) return named[lower];
        var code = lower.indexOf("#x") === 0 ? parseInt(lower.slice(2), 16) : parseInt(lower.slice(1), 10);
        return code > 0 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff) ? String.fromCodePoint(code) : "";
    });
}
function textFromHtml(value) {
    return decodeEntities(value.replace(/<[^>]*>/g, " ")).replace(/\s+/g, " ").trim();
}
function attribute(tag, name) {
    var pattern = new RegExp("\\b" + name + "\\s*=\\s*([\\\"'])(.*?)\\1", "i");
    var match = pattern.exec(tag);
    return match ? decodeEntities(match[2]) : "";
}
function seriesUrl(raw) {
    var path = raw.replace(/^https:\/\/(?:www\.)?novelupdates\.com/i, "");
    var match = /^\/series\/([a-z0-9][a-z0-9-]{0,199})\/?(?:[?#].*)?$/i.exec(path);
    return match ? {id: match[1].toLowerCase(), url: "https://www.novelupdates.com/series/" + match[1].toLowerCase() + "/"} : null;
}
function mapSearch(html) {
    if (typeof html !== "string" || html.length > 512 * 1024) throw new Error("Unexpected Novel Updates search response");
    if (/cf-chl-|challenge-platform|just a moment|captcha|access denied/i.test(html))
        throw new Error("Novel Updates blocked automated access; open the site in a browser instead");
    if (!html.trim() || html.trim() === "0" || /no (?:series|results|matches) (?:found|matched)/i.test(html))
        return [];
    var items = html.match(/<li\b(?=[^>]*\bclass\s*=\s*["'][^"']*\bsearch_li_results\b[^"']*["'])[^>]*>[\s\S]*?<\/li\s*>/gi) || [];
    if (!items.length) throw new Error("Unexpected Novel Updates search markup; the site may have changed");
    var rows = [], seen = Object.create(null);
    items.forEach(function(item) {
        var anchors = item.match(/<a\b[^>]*>[\s\S]*?<\/a\s*>/gi) || [];
        for (var i = 0; i < anchors.length; ++i) {
            var open = anchors[i].slice(0, anchors[i].indexOf(">") + 1);
            if (!/(?:^|\s)a_search(?:\s|$)/.test(attribute(open, "class"))) continue;
            var link = seriesUrl(attribute(open, "href"));
            var title = textFromHtml(anchors[i].slice(open.length).replace(/<\/a\s*>$/i, ""));
            if (!link || !title || seen[link.id]) continue;
            seen[link.id] = true;
            rows.push({id: link.id, title: title, description: "Novel Updates series",
                url: link.url, type: "book", additionalData: {attribution: "Novel Updates"}});
            break;
        }
    });
    return rows.slice(0, 30);
}
function search(query) {
    if (typeof query !== "string" || !query.trim()) return [];
    query = query.trim();
    if (query.length > 200) throw new Error("Novel Updates search query is too long");
    if (Date.now() < retryAt)
        throw new Error("Novel Updates rate limited; retry after " + Math.ceil((retryAt - Date.now()) / 1000) + " seconds");
    if (cache[query] && Date.now() - cache[query].at < 60000) return cache[query].rows;
    var body = "action=nd_ajaxsearchmain&strType=desktop&strOne=" + encodeURIComponent(query) + "&strSearchType=series";
    return host.fetch(endpoint, {
        method: "POST", body: body, minimumIntervalMs: 2500, timeoutMs: 10000,
        headers: {"Content-Type": "application/x-www-form-urlencoded; charset=UTF-8",
            "Accept": "text/html", "User-Agent": "KLookup/0.1 (https://github.com/arran4/klookup)"}
    }).then(function(response) {
        if (response.status === 429) {
            var seconds = 60;
            Object.keys(response.headers || {}).forEach(function(key) {
                if (key.toLowerCase() === "retry-after") {
                    var value = Number(response.headers[key]);
                    if (Number.isFinite(value) && value > 0) seconds = Math.min(3600, value);
                }
            });
            retryAt = Date.now() + seconds * 1000;
            throw new Error("Novel Updates rate limited (HTTP 429); retry after " + Math.ceil(seconds) + " seconds");
        }
        if (response.status === 403) throw new Error("Novel Updates denied access (HTTP 403); no anti-bot bypass is attempted");
        if (!response.ok) throw new Error("Novel Updates search: HTTP " + response.status);
        return response.text();
    }).then(function(html) {
        var rows = mapSearch(html);
        if (Object.keys(cache).length >= 16) cache = Object.create(null);
        cache[query] = {at: Date.now(), rows: rows};
        return rows;
    });
}
export { search };
export function selfTest(fixture) {
    if (!fixture) throw new Error("Missing Novel Updates fixtures");
    var rows = mapSearch(fixture.valid), partial = mapSearch(fixture.partial), rejected = false;
    try { mapSearch(fixture.invalid); } catch (_) { rejected = true; }
    return {passed: rejected && mapSearch(fixture.empty).length === 0 && rows.length === 1 && partial.length === 1 &&
        rows[0].id === fixture.expected.id && rows[0].title === fixture.expected.title &&
        rows[0].url === fixture.expected.url && partial[0].id === fixture.expected.id};
}
