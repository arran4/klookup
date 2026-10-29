// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
var counter = 0;
function search(query) {
    if (query === "microtask-storm") {
        function again() { return Promise.resolve().then(again); }
        return again();
    }
    if (query === "boundary") return [{id: "boundary", title: [typeof host.enterOperation, typeof __klookupBridge, typeof createDeferred].join(",")}];
    if (query === "sync") return [];
    if (query === "never") return new Promise(function() {});
    if (query === "spin") { while (true) {} }
    if (query === "microtask-spin") return Promise.resolve().then(function() { while (true) {} });
    if (query === "detached") {
        host.fetch("https://api.lifecycle.test/detached-a");
        host.fetch("https://api.lifecycle.test/detached-b");
        return Promise.resolve([]);
    }
    if (query.indexOf("chain-") === 0) {
        return host.fetch("https://api.lifecycle.test/first/" + query).then(function(response) {
            return response.json();
        }).then(function() {
            return host.fetch("https://api.lifecycle.test/second/" + query);
        }).then(function(response) { return response.json(); });
    }
    if (query === "fetch") return host.fetch("https://api.lifecycle.test/slow").then(function(response) { return response.json(); });
    if (query === "state") return Promise.resolve([{id: String(++counter), title: "State retained"}]);
    return Promise.resolve([]);
}
function getDetails(id) {
    if (id === "never") return new Promise(function() {});
    return Promise.resolve({result: {id: id}, content: {format: "plainText", description: "Details content"}});
}
export {search, getDetails};
