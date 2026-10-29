// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

function search(query) {
    if (query === "throw_error") {
        return Promise.reject(new Error("Script error from search"));
    }
    if (query === "fetch_test") {
        return host.fetch("https://api.sample.test/data")
            .then(function(resp) { return resp.json(); });
    }
    return Promise.resolve([
        {
            id: "js-1",
            title: "JS Result: " + query,
            description: "Result generated from ECMAScript module",
            url: "https://example.org/js-1",
            type: "Scripted"
        }
    ]);
}

function getPage(query, pageToken) {
    return Promise.resolve({
        results: [
            {
                id: "js-paged-" + pageToken,
                title: "JS Page for " + pageToken,
                type: "Scripted"
            }
        ],
        nextToken: "next-page-tok"
    });
}

function getDetails(resultId) {
    return Promise.resolve({
        result: {
            id: resultId,
            title: "Details for " + resultId
        },
        metadata: {
            attribution: ["JS Community"],
            sections: [
                {
                    label: "Runtime",
                    values: [{ text: "QJSEngine", kind: "text" }]
                }
            ]
        }
    });
}

function getContent(resultId, contentToken) {
    return Promise.resolve("# Markdown Content\n\nFetched lazily for result " + resultId);
}

export { search, getPage, getDetails, getContent };
