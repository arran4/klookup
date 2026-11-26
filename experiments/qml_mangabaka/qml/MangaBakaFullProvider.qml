// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Approach B: Proof-of-concept showing QML-hosted logic + presentation transforming raw JSON.
// Demonstrated strictly for architectural & security evaluation (#206).
Rectangle {
    id: root
    objectName: "approachBFullProvider"
    color: "transparent"

    readonly property string approachName: "Approach B (QML-hosted logic + view PoC)"

    function normalizeSeries(fixtureMap) {
        if (!fixtureMap)
            return { title: "", subtitle: "", badgeCount: 0 };
        var d = fixtureMap.data ? fixtureMap.data : fixtureMap;
        var genreList = d.genres || [];
        return {
            title: (d.title || "Unknown") + " (" + (d.type || "series") + ")",
            subtitle: (d.romanized_title || "") + " — Rating " + (d.rating || 0) + "/100",
            badgeCount: genreList.length
        };
    }

    readonly property var normalized: normalizeSeries(typeof rawFixture !== "undefined" ? rawFixture : null)
    readonly property string computedTitle: normalized.title
    readonly property int computedGenreCount: normalized.badgeCount

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            objectName: "approachBTitle"
            text: root.normalized.title
            font.pixelSize: 18
            font.bold: true
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            objectName: "approachBSubtitle"
            text: root.normalized.subtitle
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            text: "Note: QML can execute imperative JS, instantiate QtQuick items, and perform network fetches unless restricted."
            wrapMode: Text.WordWrap
            font.italic: true
            Layout.fillWidth: true
        }
    }
}
