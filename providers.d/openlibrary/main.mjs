// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

var cache = Object.create(null);
var nextRequestAt = 0;
var retryAt = 0;
var size = 10;
function mapBooks(data, offset) {
    if (!data || !Array.isArray(data.docs)) throw new Error("Open Library returned an invalid book response");
    var rows = data.docs.filter(function(book) {
        return book && typeof book.key === "string" && /^\/(?:works\/OL[0-9]+W|books\/OL[0-9]+M)$/.test(book.key) && typeof book.title === "string" && book.title.length;
    }).map(function(book) {
        var authors = Array.isArray(book.author_name) ? book.author_name.filter(function(name) { return typeof name === "string" && name.length; }) : [];
        var extra = {authors: authors, attribution: "Open Library / Internet Archive"};
        if (Number.isInteger(book.first_publish_year) && book.first_publish_year > 0) extra.first_publish_year = book.first_publish_year;
        return {id: book.key, title: book.title, description: authors.length ? "Author(s): " + authors.join(", ") : "Book record",
            url: "https://openlibrary.org" + book.key, type: "book", additionalData: extra};
    });
    var total = typeof data.numFound === "number" ? data.numFound : data.num_found;
    return {results: rows, nextToken: data.docs.length && Number.isFinite(total) && offset + size < total ? String(offset + size) : ""};
}
function fetchPage(query, offset) {
    if (typeof query !== "string" || !query.trim()) return {results: [], nextToken: ""};
    var key = JSON.stringify([query, offset]);
    if (Date.now() < retryAt) throw new Error("Open Library rate limited; retry after " + Math.ceil((retryAt - Date.now()) / 1000) + " seconds");
    if (cache[key] && Date.now() - cache[key].at < 60000) return cache[key].page;
    // Unidentified applications use the documented default one request/second.
    if (Date.now() < nextRequestAt) throw new Error("Open Library: wait one second before another request");
    nextRequestAt = Date.now() + 1000;
    var url = "https://openlibrary.org/search.json?q=" + encodeURIComponent(query) + "&limit=10&offset=" + offset + "&fields=key,title,author_name,first_publish_year";
    return host.fetch(url, {headers: {"User-Agent": "KLookup/0.1 (https://github.com/arran4/klookup)"}}).then(function(response) {
        if (response.status === 429) {
            var keys = Object.keys(response.headers || {}), seconds = 60;
            keys.forEach(function(key) { if (key.toLowerCase() === "retry-after") { var n = Number(response.headers[key]); if (isFinite(n) && n > 0) seconds = n; } });
            retryAt = Date.now() + seconds * 1000;
            throw new Error("Open Library rate limited (HTTP 429); retry after " + Math.ceil(seconds) + " seconds");
        }
        if (!response.ok) throw new Error("Open Library API: HTTP " + response.status);
        return response.json();
    }).then(function(data) {
        var page = mapBooks(data, offset);
        if (Object.keys(cache).length >= 16) cache = Object.create(null);
        cache[key] = {at: Date.now(), page: page}; return page;
    });
}
export function search(query) { return fetchPage(query, 0); }
export function getPage(query, token) {
    if (token === "") return fetchPage(query, 0);
    if (!/^(?:0|[1-9][0-9]*)$/.test(token) || !Number.isSafeInteger(Number(token)) || Number(token) > Number.MAX_SAFE_INTEGER - size || Number(token) % size) throw new Error("Invalid Open Library page token");
    return fetchPage(query, Number(token));
}
export function selfTest(fixture) {
    if (!fixture) throw new Error("Missing Open Library fixtures");
    var page = mapBooks(fixture.valid, 0), partial = mapBooks(fixture.partial, 0);
    var rejected = false; try { mapBooks(fixture.invalid, 0); } catch (_) { rejected = true; }
    return {passed: rejected && mapBooks(fixture.empty, 0).results.length === 0 && page.results.length === 1 && partial.results.length === 1 &&
        page.results[0].id === fixture.expected.id && page.results[0].title === fixture.expected.title && page.results[0].url === fixture.expected.url &&
        page.results[0].additionalData.first_publish_year === 1984 && page.results[0].additionalData.authors[0] === "Author 雪"};
}
