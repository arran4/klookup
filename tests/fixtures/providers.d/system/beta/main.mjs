// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

export async function search(query) {
    return { results: [{ id: "beta-1", title: `Beta Result for ${query}` }] };
}
