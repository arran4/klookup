// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

var storeCache = null, cache = Object.create(null), retryAt = 0;
function dealToken(id) {
    if (typeof id !== "string" || !id.length || id.length > 2048) throw new Error("Invalid CheapShark deal ID");
    // CheapShark returns already-encoded deal IDs. Canonicalize exactly once.
    return encodeURIComponent(decodeURIComponent(id));
}
function dealUrl(id) { return "https://www.cheapshark.com/redirect?dealID=" + dealToken(id); }
function money(value) { return typeof value === "string" && /^[0-9]+(?:\.[0-9]{1,2})?$/.test(value) && isFinite(Number(value)); }
function mapStores(data) {
    if (!Array.isArray(data)) throw new Error("Invalid CheapShark stores response");
    var stores = Object.create(null);
    data.forEach(function(store) { if (store && /^[1-9][0-9]*$/.test(String(store.storeID)) && typeof store.storeName === "string" && store.storeName.length) stores[String(store.storeID)] = store.storeName; });
    return stores;
}
function mapDeals(data, stores) {
    if (!Array.isArray(data)) throw new Error("Invalid CheapShark deals response");
    var rows = [];
    data.forEach(function(deal) {
        if (!deal || typeof deal.title !== "string" || !deal.title.length || !money(deal.salePrice) || !money(deal.normalPrice) || !/^[1-9][0-9]*$/.test(String(deal.gameID)) || !/^[1-9][0-9]*$/.test(String(deal.storeID))) return;
        var url; try { url = dealUrl(deal.dealID); } catch (_) { return; }
        var store = stores[String(deal.storeID)] || "Store ID " + deal.storeID;
        var extra = {game_id: String(deal.gameID), store_id: String(deal.storeID), store: store, deal_id: deal.dealID,
            sale_price_usd: Number(deal.salePrice), regular_price_usd: Number(deal.normalPrice), currency: "USD", attribution: "CheapShark"};
        if (deal.savings !== undefined && isFinite(Number(deal.savings))) extra.savings_percent = Number(deal.savings);
        if (Number.isInteger(deal.releaseDate) && deal.releaseDate > 0) extra.release_timestamp = deal.releaseDate;
        if (deal.metacriticScore !== undefined && /^[0-9]+$/.test(String(deal.metacriticScore))) extra.metacritic_score = Number(deal.metacriticScore);
        rows.push({id: deal.dealID, title: deal.title, description: store + " — USD " + Number(deal.salePrice).toFixed(2) + " (regular USD " + Number(deal.normalPrice).toFixed(2) + ") · CheapShark",
            url: url, type: "game", additionalData: extra});
    });
    return rows;
}
function read(path) {
    if (Date.now() < retryAt) throw new Error("CheapShark rate limited; retry after " + Math.ceil((retryAt - Date.now()) / 1000) + " seconds");
    return host.fetch("https://www.cheapshark.com/api/1.0/" + path, {headers: {"User-Agent": "KLookup/0.1 (https://github.com/arran4/klookup)", "Accept": "application/json"}, timeoutMs: 5000}).then(function(response) {
        if (response.status === 429) {
            var seconds = 60;
            Object.keys(response.headers || {}).forEach(function(key) { if (key.toLowerCase() === "retry-after") { var n = Number(response.headers[key]); if (isFinite(n) && n > 0) seconds = n; } });
            retryAt = Date.now() + seconds * 1000;
            var error = new Error("CheapShark rate limited (HTTP 429); retry after " + Math.ceil(seconds) + " seconds"); error.rateLimited = true; throw error;
        }
        if (!response.ok) throw new Error("CheapShark API: HTTP " + response.status);
        return response.json();
    });
}
function stores() {
    if (storeCache && Date.now() - storeCache.at < 86400000) return Promise.resolve(storeCache.stores);
    // Cache fulfilled reference data only; cancellation must not retain a never-settling request.
    return read("stores").then(mapStores).then(function(value) { storeCache = {at: Date.now(), stores: value}; return value; }, function(error) { if (error && error.rateLimited) throw error; return Object.create(null); });
}
export function search(query) {
    if (typeof query !== "string" || !query.trim()) return [];
    if (Date.now() < retryAt) throw new Error("CheapShark rate limited; retry after " + Math.ceil((retryAt - Date.now()) / 1000) + " seconds");
    if (cache[query] && Date.now() - cache[query].at < 60000) return cache[query].rows;
    return Promise.all([stores(), read("deals?title=" + encodeURIComponent(query) + "&pageNumber=0&pageSize=10")]).then(function(values) {
        var rows = mapDeals(values[1], values[0]);
        if (Object.keys(cache).length >= 16) cache = Object.create(null);
        cache[query] = {at: Date.now(), rows: rows}; return rows;
    });
}
function mapDetails(data, id, stores) {
    if (!data || !data.gameInfo || typeof data.gameInfo.name !== "string") throw new Error("Invalid CheapShark deal detail response");
    var info = data.gameInfo;
    var rows = mapDeals([{title: info.name, dealID: id, gameID: info.gameID, storeID: info.storeID, salePrice: info.salePrice, normalPrice: info.retailPrice}], stores);
    if (!rows.length) throw new Error("Incomplete CheapShark deal details");
    var row = rows[0];
    var values = [{text: row.description, kind: "text"}, {text: "Open CheapShark deal", kind: "link", url: row.url}];
    if (data.cheapestPrice && money(data.cheapestPrice.price)) values.push({text: "Lowest recorded: USD " + Number(data.cheapestPrice.price).toFixed(2), kind: "text"});
    return {result: row, metadata: {attribution: ["CheapShark"], sections: [{label: "PC game pricing (USD)", values: values}]}};
}
export function getDetails(id) {
    var token = dealToken(id);
    return Promise.all([stores(), read("deals?id=" + token)]).then(function(values) { return mapDetails(values[1], id, values[0]); });
}
export function selfTest(fixture) {
    if (!fixture) throw new Error("Missing CheapShark fixtures");
    var names = mapStores(fixture.stores), rows = mapDeals(fixture.valid, names), partial = mapDeals(fixture.partial, names), rejected = false;
    try { mapDeals(fixture.invalid, names); } catch (_) { rejected = true; }
    var details = mapDetails(fixture.details, fixture.expected.id, names);
    return {passed: rejected && mapDeals(fixture.empty, names).length === 0 && rows.length === 1 && partial.length === 1 &&
        rows[0].id === fixture.expected.id && rows[0].title === fixture.expected.title && rows[0].url === fixture.expected.url && rows[0].additionalData.currency === "USD" &&
        rows[0].additionalData.sale_price_usd === 8.99 && rows[0].additionalData.store === "Steam" && details.result.url === fixture.expected.url};
}
