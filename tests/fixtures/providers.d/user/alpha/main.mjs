// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

export async function search(query) {
    return { results: [{ id: "alpha-1", title: `Alpha Result for ${query}` }] };
}
