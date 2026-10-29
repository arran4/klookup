// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

function stripHtml(html) {
    if (!html) return "";
    return html.replace(/<[^>]*>/g, "");
}

function search(query) {
    var url = "https://api.tvmaze.com/search/shows?q=" + encodeURIComponent(query);
    return host.fetch(url).then(function(resp) {
        if (!resp.ok) {
            throw new Error("TVMaze API error: HTTP " + resp.status);
        }
        return resp.json();
    }).then(mapSearch);
}

function mapSearch(data) {
        if (!Array.isArray(data)) {
            throw new Error("TVMaze returned an invalid search response");
        }
        var results = [];
        for (var i = 0; i < data.length; ++i) {
            var item = data[i];
            if (!item || !item.show) continue;
            var show = item.show;

            var desc = stripHtml(show.summary || "");
            if (desc.length > 200) {
                desc = desc.substring(0, 200) + "...";
            }

            var additional = {
                language: show.language || "",
                premiered: show.premiered || "",
                status: show.status || ""
            };
            if (show.rating && show.rating.average !== null && show.rating.average !== undefined) {
                additional.rating = Number(show.rating.average);
            }

            results.push({
                id: String(show.id),
                title: show.name || "",
                description: desc,
                url: show.url || "",
                type: "tvshow",
                additionalData: additional
            });
        }
        return results;
}

function getDetails(resultId) {
    var url = "https://api.tvmaze.com/shows/" + encodeURIComponent(resultId);
    return host.fetch(url).then(function(resp) {
        if (!resp.ok) {
            throw new Error("TVMaze show fetch error: HTTP " + resp.status);
        }
        return resp.json();
    }).then(function(show) {
        var values = [];
        if (show.genres && show.genres.length > 0) {
            values.push({ text: show.genres.join(", "), kind: "text" });
        }
        if (show.network && show.network.name) {
            values.push({ text: show.network.name, kind: "text" });
        }
        if (show.officialSite) {
            values.push({ text: "Official Website", url: show.officialSite, kind: "link" });
        }
        if (show.url) {
            values.push({ text: "TVMaze Page", url: show.url, kind: "link" });
        }

        var sections = [];
        if (values.length > 0) {
            sections.push({
                label: "Show Details",
                values: values
            });
        }

        var desc = stripHtml(show.summary || "");
        var markdown = "## " + (show.name || "Show") + "\n\n" + desc;
        var imgUrl = "";
        if (show.image) {
            imgUrl = show.image.medium || show.image.original || "";
        }
        if (imgUrl) {
            markdown = "![Poster](" + imgUrl + ")\n\n" + markdown;
        }

        return {
            result: {
                id: String(show.id),
                title: show.name || "",
                url: show.url || "",
                type: "tvshow"
            },
            metadata: {
                attribution: ["TVMaze.com"],
                sections: sections
            },
            content: {
                format: "markdown",
                description: markdown
            }
        };
    });
}

function getPage(query, pageToken) {
    return search(query);
}

function getContent(resultId, contentToken) {
    return getDetails(resultId).then(function(details) {
        return {
            resultId: resultId,
            content: details.content,
            attribution: "TVMaze.com"
        };
    });
}

export { search, getPage, getDetails, getContent };

export function selfTest(fixture) {
    if (!fixture || !Array.isArray(fixture.search)) throw new Error("Missing TVMaze diagnostic fixture");
    var rows = mapSearch(fixture.search);
    var rejected = false;
    try { mapSearch({error: "wrong shape"}); } catch (_) { rejected = true; }
    return {passed: rejected && mapSearch([]).length === 0 && rows.length === 1 && rows[0].id === "42" && rows[0].title === "Fixture 雪" && rows[0].additionalData.rating === 8};
}
