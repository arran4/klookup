// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Approach A: C++ / Host logic provides structured properties via MangaBakaViewModel;
// QML handles rich declarative presentation (#135, #144, #146) purely as a view layer.
ScrollView {
    id: root
    objectName: "approachADetailView"
    contentWidth: availableWidth
    clip: true

    readonly property string approachName: "Approach A (C++/JS logic + QML detail view)"

    ColumnLayout {
        width: root.availableWidth - 24
        anchors.margins: 12
        spacing: 10

        Label {
            id: titleHeader
            objectName: "primaryTitleLabel"
            text: mangaModel ? (mangaModel.primaryTitle + " (" + mangaModel.formatType + ")") : ""
            font.pixelSize: 20
            font.bold: true
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // Metadata badges (#135 rating, status, content rating, language, year)
        Flow {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: mangaModel ? [
                    "Status: " + mangaModel.status,
                    "Rating: " + Number(mangaModel.rating).toFixed(1) + " (" + mangaModel.ratingCount + " votes)",
                    "Content: " + mangaModel.contentRating,
                    "Lang: " + mangaModel.originalLanguage,
                    "Year: " + mangaModel.year
                ] : []

                delegate: Rectangle {
                    radius: 4
                    color: "#2d3748"
                    implicitWidth: badgeText.implicitWidth + 12
                    implicitHeight: badgeText.implicitHeight + 6
                    Label {
                        id: badgeText
                        anchors.centerIn: parent
                        text: modelData
                        color: "#edf2f7"
                        font.pixelSize: 11
                    }
                }
            }
        }

        Label {
            objectName: "summaryLabel"
            text: mangaModel ? mangaModel.summary : ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // Expandable multi-language & romanized titles section (#144)
        Frame {
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 4
                Label {
                    text: "Alternate & Native Titles (#144)"
                    font.bold: true
                }
                Label {
                    objectName: "romanizedTitleLabel"
                    text: mangaModel ? ("Romanized: " + mangaModel.romanizedTitle + " | Native: " + mangaModel.nativeTitle) : ""
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Repeater {
                    model: mangaModel ? mangaModel.alternateTitles : []
                    delegate: Label {
                        text: "• [" + modelData.language + "] " + modelData.title + (modelData.isPrimary ? " (Primary)" : "")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Rich metadata & genres (#146)
        Frame {
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 4
                Label {
                    text: "Genres & Creators (#146)"
                    font.bold: true
                }
                Label {
                    objectName: "genresLabel"
                    text: mangaModel ? ("Genres: " + mangaModel.genres.join(", ") + "\nTags: " + mangaModel.tags.join(", ")) : ""
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "creatorsLabel"
                    text: mangaModel ? mangaModel.creators.join(" | ") : ""
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
