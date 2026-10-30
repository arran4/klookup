// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

var cached = Object.create(null);
var retryAt = 0;
function header(response, name) {
    var keys = Object.keys(response.headers || {});
    for (var i = 0; i < keys.length; ++i) if (keys[i].toLowerCase() === name.toLowerCase()) return response.headers[keys[i]];
    return "";
}
function mapSuggestions(data) {
    if (!data || !Array.isArray(data.data)) throw new Error("Lyrics.ovh returned an invalid suggestion response");
    return data.data.filter(function(track) {
        return track && /^(?:[1-9][0-9]*)$/.test(String(track.id)) && typeof track.title === "string" && track.title.length &&
            typeof track.link === "string" && /^https?:\/\/(?:www\.)?deezer\.com\/(?:[a-z]{2}\/)?track\/[0-9]+(?:[?#].*)?$/.test(track.link);
    }).map(function(track) {
        var artist = track.artist && typeof track.artist.name === "string" ? track.artist.name : "";
        var album = track.album && typeof track.album.title === "string" ? track.album.title : "";
        return {id: String(track.id), title: track.title, description: artist ? "Song by " + artist : "Song suggestion",
            url: track.link.replace(/^http:/, "https:"), type: "song",
            additionalData: {artist: artist, album: album, attribution: "Lyrics.ovh / Deezer"}};
    });
}
export function search(query) {
    if (typeof query !== "string" || !query.trim()) return [];
    if (Date.now() < retryAt) throw new Error("Lyrics.ovh rate limited; retry after " + Math.ceil((retryAt - Date.now()) / 1000) + " seconds");
    if (cached[query] && Date.now() - cached[query].at < 60000) return cached[query].rows;
    return host.fetch("https://api.lyrics.ovh/suggest/" + encodeURIComponent(query), {headers: {"User-Agent": "KLookup/0.1 (https://github.com/arran4/klookup)"}}).then(function(response) {
        if (response.status === 429) {
            var seconds = Number(header(response, "Retry-After"));
            if (!isFinite(seconds) || seconds <= 0) seconds = 60;
            retryAt = Date.now() + seconds * 1000;
            throw new Error("Lyrics.ovh rate limited (HTTP 429); retry after " + Math.ceil(seconds) + " seconds");
        }
        if (!response.ok) throw new Error("Lyrics.ovh suggestion API: HTTP " + response.status);
        return response.json();
    }).then(function(data) {
        var rows = mapSuggestions(data);
        if (Object.keys(cached).length >= 16) cached = Object.create(null);
        cached[query] = {at: Date.now(), rows: rows};
        return rows;
    });
}
export function selfTest(fixture) {
    if (!fixture) throw new Error("Missing Lyrics.ovh fixtures");
    var valid = mapSuggestions(fixture.valid);
    var partial = mapSuggestions(fixture.partial);
    var rejected = false;
    try { mapSuggestions(fixture.invalid); } catch (_) { rejected = true; }
    return {passed: rejected && mapSuggestions(fixture.empty).length === 0 && valid.length === 1 && partial.length === 1 &&
        valid[0].id === fixture.expected.id && valid[0].title === fixture.expected.title && valid[0].url === fixture.expected.url &&
        valid[0].additionalData.artist === fixture.expected.additionalData.artist && valid[0].additionalData.album === fixture.expected.additionalData.album};
}
