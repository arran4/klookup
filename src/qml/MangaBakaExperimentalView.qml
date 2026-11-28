// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Host-controlled QML detail surface for MangaBaka Experimental (#206).
// Executed exclusively from compiled application Qt resources (qrc:/),
// consuming validated properties and safe invokables from MangaBakaExperimentalModel.
ScrollView {
    id: root
    objectName: "mangaBakaExperimentalRoot"
    contentWidth: availableWidth
    clip: true

    component ThemedButton : Button {
        id: tb
        property bool checkedHighlight: false
        implicitHeight: Math.max(28, tbLabel.implicitHeight + 10)
        implicitWidth: tbLabel.implicitWidth + 18
        contentItem: Label {
            id: tbLabel
            text: tb.text
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: tb.checkedHighlight
                   ? (mangaModel ? mangaModel.colorHighlightedText : "#ffffff")
                   : (mangaModel ? mangaModel.colorButtonText : "#111111")
            opacity: tb.enabled ? 1.0 : 0.5
        }
        background: Rectangle {
            radius: 4
            color: {
                if (tb.checkedHighlight)
                    return mangaModel ? mangaModel.colorHighlight : "#3584e4";
                if (tb.down)
                    return mangaModel ? mangaModel.colorMid : "#cccccc";
                if (tb.hovered)
                    return mangaModel ? mangaModel.colorAlternateBase : "#e8e8e8";
                return mangaModel ? mangaModel.colorButton : "#e0e0e0";
            }
            border.color: (tb.activeFocus || tb.checkedHighlight)
                          ? (mangaModel ? mangaModel.colorHighlight : "#3584e4")
                          : (mangaModel ? mangaModel.colorMid : "#999999")
            border.width: 1
        }
    }

    Rectangle {
        anchors.fill: parent
        color: mangaModel ? mangaModel.colorWindow : "#ffffff"
        z: -1
    }

    ColumnLayout {
        width: Math.max(240, root.availableWidth - 24)
        x: 12
        y: 12
        spacing: mangaModel && mangaModel.compactLayout ? 8 : 12

        // 1. Header: Canonical/Preferred Title, Original Title, Artwork & Visit Source
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            // Click-to-load remote cover artwork placeholder / frame (#146)
            Rectangle {
                id: artworkBox
                Layout.preferredWidth: 112
                Layout.preferredHeight: 158
                Layout.alignment: Qt.AlignTop
                radius: 6
                color: mangaModel ? mangaModel.colorAlternateBase : "#f0f0f0"
                border.color: mangaModel ? mangaModel.colorMid : "#bbbbbb"
                border.width: 1
                visible: !mangaModel || mangaModel.artworkState !== "none"

                Image {
                    anchors.fill: parent
                    anchors.margins: 2
                    fillMode: Image.PreserveAspectFit
                    source: mangaModel ? mangaModel.artworkSource : ""
                    visible: mangaModel && mangaModel.artworkState === "loaded" && mangaModel.artworkSource !== ""
                    asynchronous: false
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    visible: !mangaModel || mangaModel.artworkState !== "loaded"

                    Label {
                        Layout.fillWidth: true
                        text: {
                            if (!mangaModel) return "";
                            if (mangaModel.artworkState === "loading") return qsTr("Loading cover…");
                            if (mangaModel.artworkState === "error") return mangaModel.artworkError;
                            return qsTr("Cover image blocked for privacy");
                        }
                        color: mangaModel ? mangaModel.colorMutedText : "#666666"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ThemedButton {
                        objectName: "loadArtworkButton"
                        Layout.alignment: Qt.AlignHCenter
                        text: mangaModel && mangaModel.artworkState === "error" ? qsTr("Retry artwork") : qsTr("Load artwork")
                        enabled: mangaModel && mangaModel.artworkState !== "loading" && mangaModel.coverUrl !== ""
                        onClicked: mangaModel.loadArtwork()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        objectName: "experimentalBadge"
                        text: qsTr("EXPERIMENTAL")
                        font.pixelSize: 10
                        font.bold: true
                        padding: 4
                        color: mangaModel ? mangaModel.colorHighlightedText : "#ffffff"
                        background: Rectangle {
                            radius: 4
                            color: mangaModel ? mangaModel.colorHighlight : "#3584e4"
                        }
                    }

                    Label {
                        objectName: "formatTypeLabel"
                        text: mangaModel ? mangaModel.formatLabel : ""
                        font.pixelSize: 12
                        font.bold: true
                        color: mangaModel ? mangaModel.colorMutedText : "#666666"
                        visible: text !== ""
                    }

                    Item { Layout.fillWidth: true }

                    ThemedButton {
                        objectName: "visitSourceButton"
                        text: qsTr("Visit source")
                        visible: mangaModel && mangaModel.canonicalUrl !== ""
                        onClicked: mangaModel.openCanonicalSource()
                    }

                    ThemedButton {
                        objectName: "viewRawDataButton"
                        text: qsTr("View raw data")
                        visible: mangaModel && mangaModel.hasRawSources
                        onClicked: mangaModel.openRawData()
                    }
                }

                Label {
                    objectName: "preferredTitleLabel"
                    Layout.fillWidth: true
                    text: mangaModel ? mangaModel.preferredTitle : ""
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorText : "#111111"
                    font.pixelSize: 19
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    objectName: "originalTitleLabel"
                    Layout.fillWidth: true
                    text: mangaModel && mangaModel.originalTitle !== ""
                          ? (mangaModel.originalLanguage !== ""
                             ? mangaModel.originalTitle + "  [" + mangaModel.originalLanguage + "]"
                             : mangaModel.originalTitle)
                          : ""
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorMutedText : "#555555"
                    font.pixelSize: 13
                    font.italic: true
                    wrapMode: Text.WordWrap
                    visible: text !== ""
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: {
                            if (!mangaModel) return [];
                            var list = [];
                            if (mangaModel.ratingText !== "")
                                list.push(mangaModel.ratingCount > 0
                                    ? qsTr("Rating %1 (%2 votes)").arg(mangaModel.ratingText).arg(mangaModel.ratingCount)
                                    : qsTr("Rating %1").arg(mangaModel.ratingText));
                            if (mangaModel.status !== "") list.push(mangaModel.status);
                            if (mangaModel.publishedText !== "") list.push(mangaModel.publishedText);
                            else if (mangaModel.yearText !== "") list.push(mangaModel.yearText);
                            if (mangaModel.contentRating !== "") list.push(qsTr("Content: %1").arg(mangaModel.contentRating));
                            if (mangaModel.volumeChapterText !== "") list.push(mangaModel.volumeChapterText);
                            if (mangaModel.licensingText !== "") list.push(mangaModel.licensingText);
                            return list;
                        }
                        delegate: Rectangle {
                            radius: 4
                            color: mangaModel ? mangaModel.colorAlternateBase : "#eeeeee"
                            border.color: mangaModel ? mangaModel.colorMid : "#cccccc"
                            border.width: 1
                            implicitWidth: badgeLbl.implicitWidth + 12
                            implicitHeight: badgeLbl.implicitHeight + 6
                            Label {
                                id: badgeLbl
                                anchors.centerIn: parent
                                text: modelData
                                textFormat: Text.PlainText
                                color: mangaModel ? mangaModel.colorText : "#111111"
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                // Expandable titles toggle (#135, #145)
                RowLayout {
                    visible: mangaModel && mangaModel.hasMoreTitles
                    spacing: 8
                    ThemedButton {
                        objectName: "moreTitlesButton"
                        text: mangaModel && mangaModel.expandAlternateTitles
                              ? qsTr("Hide alternate titles")
                              : qsTr("More titles… (%1)").arg(mangaModel ? mangaModel.allTitles.length : 0)
                        onClicked: mangaModel.expandAlternateTitles = !mangaModel.expandAlternateTitles
                    }
                    ThemedButton {
                        objectName: "clearArtworkButton"
                        text: qsTr("Hide artwork")
                        visible: mangaModel && mangaModel.artworkState === "loaded"
                        onClicked: mangaModel.clearArtwork()
                    }
                }
            }
        }

        // Expandable Alternate Titles Card (#135)
        Rectangle {
            objectName: "alternateTitlesBox"
            Layout.fillWidth: true
            visible: mangaModel && mangaModel.expandAlternateTitles && mangaModel.allTitles.length > 0
            color: mangaModel ? mangaModel.colorBase : "#ffffff"
            border.color: mangaModel ? mangaModel.colorMid : "#cccccc"
            border.width: 1
            radius: 6
            implicitHeight: altTitlesCol.implicitHeight + 16

            ColumnLayout {
                id: altTitlesCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                Label {
                    text: qsTr("All Known Titles")
                    font.bold: true
                    color: mangaModel ? mangaModel.colorText : "#111111"
                }
                Repeater {
                    model: mangaModel ? mangaModel.allTitles : []
                    delegate: Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        color: mangaModel ? mangaModel.colorText : "#111111"
                        wrapMode: Text.WordWrap
                        text: {
                            var s = "[" + modelData.language + "] " + modelData.title;
                            if (modelData.isPrimary) s += " (primary)";
                            if (modelData.isOriginal) s += " (original)";
                            if (modelData.traitsText) s += " — " + modelData.traitsText;
                            return s;
                        }
                    }
                }
            }
        }

        // 2. Plain-Text Synopsis Card
        Rectangle {
            Layout.fillWidth: true
            visible: mangaModel && mangaModel.synopsis !== ""
            color: mangaModel ? mangaModel.colorBase : "#ffffff"
            border.color: mangaModel ? mangaModel.colorMid : "#cccccc"
            border.width: 1
            radius: 6
            implicitHeight: synCol.implicitHeight + 16

            ColumnLayout {
                id: synCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                Label {
                    text: qsTr("Synopsis")
                    font.bold: true
                    color: mangaModel ? mangaModel.colorText : "#111111"
                }
                Label {
                    objectName: "synopsisLabel"
                    Layout.fillWidth: true
                    text: mangaModel ? mangaModel.synopsis : ""
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorText : "#111111"
                    wrapMode: Text.WordWrap
                }
            }
        }

        // 3. Creators, Publishers & Upstream Source Ratings
        Rectangle {
            Layout.fillWidth: true
            visible: mangaModel && (mangaModel.authors.length > 0 || mangaModel.artists.length > 0 ||
                                    mangaModel.publishers.length > 0 || mangaModel.sourceRatings.length > 0)
            color: mangaModel ? mangaModel.colorBase : "#ffffff"
            border.color: mangaModel ? mangaModel.colorMid : "#cccccc"
            border.width: 1
            radius: 6
            implicitHeight: metaCol.implicitHeight + 16

            ColumnLayout {
                id: metaCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Label {
                    text: qsTr("Publishing & Source Ratings")
                    font.bold: true
                    color: mangaModel ? mangaModel.colorText : "#111111"
                }
                Label {
                    Layout.fillWidth: true
                    visible: mangaModel && mangaModel.authors.length > 0
                    text: qsTr("Authors: %1").arg(mangaModel ? mangaModel.authors.join(", ") : "")
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorText : "#111111"
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: mangaModel && mangaModel.artists.length > 0
                    text: qsTr("Artists: %1").arg(mangaModel ? mangaModel.artists.join(", ") : "")
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorText : "#111111"
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: mangaModel && mangaModel.publishers.length > 0
                    text: {
                        if (!mangaModel) return "";
                        var items = [];
                        for (var i = 0; i < mangaModel.publishers.length; ++i) {
                            var p = mangaModel.publishers[i];
                            items.push(p.language ? (p.name + " [" + p.language + "]") : p.name);
                        }
                        return qsTr("Publishers: %1").arg(items.join(", "));
                    }
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorText : "#111111"
                    wrapMode: Text.WordWrap
                }
                Label {
                    objectName: "sourceRatingsLabel"
                    Layout.fillWidth: true
                    visible: mangaModel && mangaModel.sourceRatings.length > 0
                    text: {
                        if (!mangaModel) return "";
                        var parts = [];
                        for (var i = 0; i < mangaModel.sourceRatings.length; ++i) {
                            var s = mangaModel.sourceRatings[i];
                            parts.push(s.rating ? (s.source + ": " + s.rating) : (s.source + " (#" + s.id + ")"));
                        }
                        return qsTr("Upstream sources: %1").arg(parts.join(" · "));
                    }
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorText : "#111111"
                    wrapMode: Text.WordWrap
                }
            }
        }

        // 4. Hierarchical Weighted Tags, Explicit/Implied Filters & Spoiler Reveal (#135)
        Rectangle {
            Layout.fillWidth: true
            visible: mangaModel && mangaModel.totalTagCount > 0
            color: mangaModel ? mangaModel.colorBase : "#ffffff"
            border.color: mangaModel ? mangaModel.colorMid : "#cccccc"
            border.width: 1
            radius: 6
            implicitHeight: tagCol.implicitHeight + 16

            ColumnLayout {
                id: tagCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: qsTr("Tags (%1)").arg(mangaModel ? mangaModel.totalTagCount : 0)
                        font.bold: true
                        color: mangaModel ? mangaModel.colorText : "#111111"
                    }
                    TextField {
                        objectName: "tagFilterInput"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Filter visible tags…")
                        color: mangaModel ? mangaModel.colorText : "#111111"
                        placeholderTextColor: mangaModel ? mangaModel.colorMutedText : "#666666"
                        background: Rectangle {
                            radius: 4
                            color: mangaModel ? mangaModel.colorBase : "#ffffff"
                            border.color: parent.activeFocus
                                          ? (mangaModel ? mangaModel.colorHighlight : "#3584e4")
                                          : (mangaModel ? mangaModel.colorMid : "#999999")
                            border.width: 1
                        }
                        text: mangaModel ? mangaModel.tagFilterText : ""
                        onTextChanged: if (mangaModel) mangaModel.tagFilterText = text
                    }
                    ThemedButton {
                        checkedHighlight: mangaModel && mangaModel.tagFilterMode === "genres"
                        text: mangaModel && mangaModel.tagFilterMode === "genres" ? qsTr("Genres ✓") : qsTr("Genres")
                        onClicked: mangaModel.tagFilterMode = (mangaModel.tagFilterMode === "genres" ? "all" : "genres")
                    }
                    ThemedButton {
                        checkedHighlight: mangaModel && mangaModel.tagFilterMode === "explicit"
                        text: mangaModel && mangaModel.tagFilterMode === "explicit" ? qsTr("Explicit ✓") : qsTr("Explicit")
                        onClicked: mangaModel.tagFilterMode = (mangaModel.tagFilterMode === "explicit" ? "all" : "explicit")
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: mangaModel ? mangaModel.visibleTags : []
                        delegate: Rectangle {
                            id: tagChip
                            property bool isSpoilerMasked: modelData.isSpoiler && !modelData.isRevealed
                            property bool isImplied: !modelData.isExplicit
                            activeFocusOnTab: true
                            radius: 4
                            color: {
                                if (isSpoilerMasked)
                                    return mangaModel ? mangaModel.colorAlternateBase : "#dddddd";
                                if (tagMouse.containsMouse || activeFocus)
                                    return mangaModel ? mangaModel.colorHighlight : "#3584e4";
                                return modelData.isGenre
                                    ? (mangaModel ? mangaModel.colorButton : "#e4e4e4")
                                    : (mangaModel ? mangaModel.colorAlternateBase : "#efefef");
                            }
                            border.color: isImplied
                                ? (mangaModel ? mangaModel.colorLink : "#1c71d8")
                                : (mangaModel ? mangaModel.colorMid : "#aaaaaa")
                            border.width: 1
                            implicitWidth: chipRow.implicitWidth + 14
                            implicitHeight: chipRow.implicitHeight + 8

                            RowLayout {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: 4
                                Label {
                                    text: modelData.displayText
                                    textFormat: Text.PlainText
                                    font.pixelSize: 11
                                    font.italic: !modelData.isExplicit || tagChip.isSpoilerMasked
                                    color: (!tagChip.isSpoilerMasked && (tagMouse.containsMouse || tagChip.activeFocus))
                                           ? (mangaModel ? mangaModel.colorHighlightedText : "#ffffff")
                                           : (mangaModel ? mangaModel.colorText : "#111111")
                                }
                                Label {
                                    visible: !tagChip.isSpoilerMasked && modelData.weightLabel !== ""
                                    text: "[" + modelData.weightLabel + (!modelData.isExplicit ? "·implied" : "") + "]"
                                    textFormat: Text.PlainText
                                    font.pixelSize: 10
                                    color: (!tagChip.isSpoilerMasked && (tagMouse.containsMouse || tagChip.activeFocus))
                                           ? (mangaModel ? mangaModel.colorHighlightedText : "#ffffff")
                                           : (mangaModel ? mangaModel.colorMutedText : "#666666")
                                }
                            }

                            onActiveFocusChanged: {
                                if (mangaModel && modelData.isSpoiler)
                                    mangaModel.peekSpoilerTag(modelData.index, activeFocus);
                            }

                            Keys.onSpacePressed: {
                                if (mangaModel) {
                                    if (modelData.isSpoiler) mangaModel.toggleSpoilerTag(modelData.index);
                                    mangaModel.selectTag(modelData.index);
                                }
                            }

                            MouseArea {
                                id: tagMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: if (mangaModel && modelData.isSpoiler) mangaModel.peekSpoilerTag(modelData.index, true)
                                onExited: if (mangaModel && modelData.isSpoiler) mangaModel.peekSpoilerTag(modelData.index, false)
                                onClicked: {
                                    if (mangaModel) {
                                        if (modelData.isSpoiler) mangaModel.toggleSpoilerTag(modelData.index);
                                        mangaModel.selectTag(modelData.index);
                                    }
                                }
                            }
                        }
                    }
                }

                // Expand lower weight tiers & toggle implied/spoiler visibility
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    ThemedButton {
                        objectName: "toggleLowerTiersButton"
                        visible: mangaModel && (mangaModel.hiddenLowerTierCount > 0 || mangaModel.showLowerTagTiers)
                        text: mangaModel && mangaModel.showLowerTagTiers
                              ? qsTr("Show top 2 tiers only")
                              : qsTr("Show all tiers (+%1)").arg(mangaModel ? mangaModel.hiddenLowerTierCount : 0)
                        onClicked: mangaModel.showLowerTagTiers = !mangaModel.showLowerTagTiers
                    }
                    ThemedButton {
                        objectName: "toggleImpliedTagsButton"
                        visible: mangaModel && (mangaModel.hiddenImpliedCount > 0 || !mangaModel.showImpliedTags)
                        text: mangaModel && mangaModel.showImpliedTags
                              ? qsTr("Hide implied tags")
                              : qsTr("Show implied tags (+%1)").arg(mangaModel ? mangaModel.hiddenImpliedCount : 0)
                        onClicked: mangaModel.showImpliedTags = !mangaModel.showImpliedTags
                    }
                    ThemedButton {
                        objectName: "toggleSpoilersButton"
                        visible: mangaModel && mangaModel.spoilerTagCount > 0
                        text: mangaModel && mangaModel.revealSpoilers
                              ? qsTr("Mask spoiler tags")
                              : qsTr("Reveal all spoilers (%1)").arg(mangaModel ? mangaModel.spoilerTagCount : 0)
                        onClicked: mangaModel.revealSpoilers = !mangaModel.revealSpoilers
                    }
                }

                Label {
                    objectName: "selectedTagDetails"
                    Layout.fillWidth: true
                    visible: mangaModel && mangaModel.selectedTagName !== ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: mangaModel ? mangaModel.colorMutedText : "#555555"
                    text: mangaModel
                          ? (mangaModel.selectedTagPath !== ""
                             ? (mangaModel.selectedTagPath + " — " + mangaModel.selectedTagDescription)
                             : (mangaModel.selectedTagName + ": " + mangaModel.selectedTagDescription))
                          : ""
                }
            }
        }

        // 5. Per-Result & Default Display Preferences Bar (#144)
        Rectangle {
            Layout.fillWidth: true
            color: mangaModel ? mangaModel.colorAlternateBase : "#f0f0f0"
            border.color: mangaModel ? mangaModel.colorMid : "#bbbbbb"
            border.width: 1
            radius: 6
            implicitHeight: prefCol.implicitHeight + 16

            ColumnLayout {
                id: prefCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Display Preferences (#144)")
                        font.bold: true
                        color: mangaModel ? mangaModel.colorText : "#111111"
                    }
                    Label {
                        text: mangaModel ? mangaModel.preferenceScopeNote : ""
                        color: mangaModel ? mangaModel.colorMutedText : "#666666"
                        font.pixelSize: 11
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    ThemedButton {
                        objectName: "prefSaveProviderButton"
                        text: qsTr("Save as MangaBaka Experimental default")
                        onClicked: mangaModel.saveAsProviderDefault()
                    }
                    ThemedButton {
                        objectName: "prefSaveGlobalButton"
                        text: qsTr("Save as global default")
                        onClicked: mangaModel.saveAsGlobalDefault()
                    }
                    ThemedButton {
                        objectName: "prefResetButton"
                        text: qsTr("Reset overrides")
                        onClicked: mangaModel.resetPerResultOverrides()
                    }
                }

                Label {
                    objectName: "prefFeedbackLabel"
                    visible: mangaModel && mangaModel.statusFeedback !== ""
                    text: mangaModel ? mangaModel.statusFeedback : ""
                    textFormat: Text.PlainText
                    color: mangaModel ? mangaModel.colorLink : "#1c71d8"
                    font.pixelSize: 11
                }
            }
        }

        // Attribution footer
        Label {
            Layout.fillWidth: true
            text: qsTr("Data provided by MangaBaka and upstream sources; third-party terms apply.")
            font.pixelSize: 11
            color: mangaModel ? mangaModel.colorMutedText : "#666666"
            wrapMode: Text.WordWrap
        }
    }
}
