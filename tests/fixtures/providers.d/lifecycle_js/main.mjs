// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
var counter = 0;
var baseline = Promise.resolve([{id: "baseline", title: "Initialized module promise"}]);
function search(query) {
    if (query === "baseline") return baseline;
    if (query === "subclass") {
        class LocalPromise extends Promise {}
        var result = LocalPromise.resolve([{id: "subclass", title: "Promise subclass"}]).then(function(value) { return value; });
        if (!(result instanceof LocalPromise)) throw new Error("Promise subclass was lost");
        return result;
    }
    if (query === "all") return Promise.all([
        Promise.resolve({id: "one", title: "Native promise"}),
        {then: function(resolve) { resolve({id: "two", title: "Thenable"}); }}
    ]);
    if (query === "thenable-spin") return Promise.resolve().then(function() { return {then: function() { while (true) {} }}; });
    if (query === "export-thenable-spin") return {then: function() { while (true) {} }};
    if (query === "resolve-thenable-spin") return Promise.resolve({then: function() { while (true) {} }});
    if (query === "nested-thenable") return Promise.resolve().then(function() {
        return {then: function(yes) { yes({then: function(done) { done([{id: "nested", title: "Nested thenable"}]); }}); }};
    });
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
