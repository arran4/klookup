// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

var cache = Object.create(null), retryAt = 0;
var document = "query ($search: String) { Page(perPage: 10) { media(search: $search, type: ANIME) { id title { romaji english } description siteUrl status episodes averageScore } } }";
function mapAnime(response) {
    if (!response || (response.errors !== undefined && !Array.isArray(response.errors)) || !response.data || !response.data.Page || !Array.isArray(response.data.Page.media)) throw new Error("AniList returned an invalid GraphQL response");
    var errors = response.errors || [];
    var rows = response.data.Page.media.filter(function(media) {
        return media && Number.isInteger(media.id) && media.id > 0 && media.title &&
            (typeof media.title.english === "string" && media.title.english.length || typeof media.title.romaji === "string" && media.title.romaji.length) &&
            typeof media.siteUrl === "string" && /^https:\/\/anilist\.co\/anime\/[0-9]+(?:[/?#].*)?$/.test(media.siteUrl);
    }).map(function(media) {
        var title = media.title.english || media.title.romaji;
        var description = typeof media.description === "string" ? media.description.replace(/<[^>]*>/g, "") : "";
        if (description.length > 200) description = description.substring(0, 200) + "...";
        var extra = {attribution: "AniList.co"};
        if (typeof media.status === "string") extra.status = media.status;
        if (Number.isInteger(media.episodes) && media.episodes >= 0) extra.episodes = media.episodes;
        if (Number.isInteger(media.averageScore)) extra.score = media.averageScore;
        if (errors.length) extra.api_warning = "Partial AniList response";
        return {id: String(media.id), title: title, description: description, url: media.siteUrl, type: "anime", additionalData: extra};
    });
    if (errors.length && !rows.length) throw new Error("AniList GraphQL request failed");
    return rows;
}
export function search(query) {
    if (typeof query !== "string" || !query.trim()) return [];
    if (Date.now() < retryAt) throw new Error("AniList rate limited; retry after " + Math.ceil((retryAt - Date.now()) / 1000) + " seconds");
    if (cache[query] && Date.now() - cache[query].at < 60000) return cache[query].rows;
    return host.fetch("https://graphql.anilist.co", {method: "POST", headers: {"Content-Type": "application/json", "Accept": "application/json", "User-Agent": "KLookup/0.1 (https://github.com/arran4/klookup)"}, body: JSON.stringify({query: document, variables: {search: query}})}).then(function(response) {
        if (response.status === 429) {
            var seconds = 60;
            Object.keys(response.headers || {}).forEach(function(key) { if (key.toLowerCase() === "retry-after") { var n = Number(response.headers[key]); if (isFinite(n) && n > 0) seconds = n; } });
            retryAt = Date.now() + seconds * 1000;
            throw new Error("AniList rate limited (HTTP 429); retry after " + Math.ceil(seconds) + " seconds");
        }
        if (!response.ok) throw new Error("AniList API: HTTP " + response.status);
        return response.json();
    }).then(function(data) {
        if (data && Array.isArray(data.errors) && data.errors.some(function(error) { return error && error.status === 429; })) { retryAt = Date.now() + 60000; throw new Error("AniList GraphQL rate limit; retry after 60 seconds"); }
        var rows = mapAnime(data);
        if (Object.keys(cache).length >= 16) cache = Object.create(null);
        cache[query] = {at: Date.now(), rows: rows}; return rows;
    });
}
export function selfTest(fixture) {
    if (!fixture) throw new Error("Missing AniList fixtures");
    var rows = mapAnime(fixture.valid), partial = mapAnime(fixture.partial), rejected = false;
    try { mapAnime(fixture.invalid); } catch (_) { rejected = true; }
    return {passed: rejected && mapAnime(fixture.empty).length === 0 && rows.length === 1 && partial.length === 1 &&
        rows[0].id === fixture.expected.id && rows[0].title === fixture.expected.title && rows[0].url === fixture.expected.url &&
        rows[0].additionalData.score === 82 && rows[0].additionalData.episodes === 12 && partial[0].additionalData.api_warning === "Partial AniList response"};
}
