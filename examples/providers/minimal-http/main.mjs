// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

function mapItems(data) {
    if (!data || !Array.isArray(data.items)) {
        throw new Error("Expected response object with an items array");
    }
    var out = [];
    for (var i = 0; i < data.items.length; ++i) {
        var entry = data.items[i];
        if (!entry || !entry.id || !entry.title) {
            continue;
        }
        out.push({
            id: String(entry.id),
            title: String(entry.title),
            description: entry.summary ? String(entry.summary) : "",
            url: entry.url ? String(entry.url) : "",
            type: "reference"
        });
    }
    return out;
}

function search(query) {
    var url = "https://api.example.invalid/v1/search?q=" + encodeURIComponent(query);
    return host.fetch(url).then(function(resp) {
        if (!resp.ok) {
            throw new Error("Example API HTTP error: " + resp.status);
        }
        return resp.json();
    }).then(mapItems);
}

function getPage(query, pageToken) {
    return search(query);
}

function getDetails(resultId) {
    var url = "https://api.example.invalid/v1/items/" + encodeURIComponent(resultId);
    return host.fetch(url).then(function(resp) {
        if (!resp.ok) {
            throw new Error("Example item HTTP error: " + resp.status);
        }
        return resp.json();
    }).then(function(item) {
        return {
            result: {
                id: String(item.id || resultId),
                title: String(item.title || resultId),
                url: item.url ? String(item.url) : "",
                type: "reference"
            },
            metadata: {
                attribution: ["Example Reference API"]
            },
            content: {
                format: "markdown",
                description: String(item.body || item.summary || "")
            }
        };
    });
}

function getContent(resultId, contentToken) {
    return getDetails(resultId).then(function(details) {
        return {
            resultId: resultId,
            content: details.content,
            attribution: "Example Reference API"
        };
    });
}

function selfTest(fixture) {
    if (!fixture || !fixture.search) {
        throw new Error("Missing diagnostic search fixture");
    }
    var mapped = mapItems(fixture.search);
    return {
        passed: mapped.length === 1 && mapped[0].id === "ex-1" && mapped[0].title === "Minimal Example Entry"
    };
}

module.exports = {
    search: search,
    getPage: getPage,
    getDetails: getDetails,
    getContent: getContent,
    selfTest: selfTest
};
