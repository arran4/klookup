// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "providerclassification.h"
#include <KLocalizedString>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

ProviderClassification ProviderClassification::builtin(const QString &id)
{
    ProviderClassification result;
    static const QMap<QString, QStringList> topics = {
        {QStringLiteral("wikipedia"), {QStringLiteral("audience:general"), QStringLiteral("topic:reference")}},
        {QStringLiteral("duckduckgo"), {QStringLiteral("audience:general")}},
        {QStringLiteral("github"), {QStringLiteral("topic:software")}},
        {QStringLiteral("tvmaze"), {QStringLiteral("topic:tv")}},
        {QStringLiteral("themoviedb"), {QStringLiteral("topic:tv"), QStringLiteral("topic:movies")}},
        {QStringLiteral("omdb"), {QStringLiteral("topic:tv"), QStringLiteral("topic:movies")}},
        {QStringLiteral("myanimelist"), {QStringLiteral("topic:anime"), QStringLiteral("topic:manga")}},
        {QStringLiteral("anilist"), {QStringLiteral("topic:anime"), QStringLiteral("topic:manga")}},
        {QStringLiteral("mangaupdates"), {QStringLiteral("topic:manga")}},
        {QStringLiteral("mangadex"), {QStringLiteral("topic:manga")}},
        {QStringLiteral("mangabaka"), {QStringLiteral("topic:manga")}},
        {QStringLiteral("openlibrary"), {QStringLiteral("topic:books")}},
        {QStringLiteral("spotify"), {QStringLiteral("topic:music"), QStringLiteral("topic:podcasts"), QStringLiteral("topic:audiobooks")}},
        {QStringLiteral("lyrics_ovh"), {QStringLiteral("topic:music"), QStringLiteral("topic:lyrics")}},
        {QStringLiteral("cheapshark"), {QStringLiteral("topic:games"), QStringLiteral("platform:pc"), QStringLiteral("currency:USD")}},
    };
    result.facets = topics.value(id);
    return result;
}

QString ProviderClassification::label(const QString &id, const QLocale &locale) const
{
    const auto custom = labels.value(id);
    const auto code = locale.bcp47Name();
    if (custom.contains(code)) return custom.value(code);
    const auto language = code.section(QLatin1Char('-'), 0, 0);
    if (custom.contains(language)) return custom.value(language);
    if (custom.contains(QStringLiteral("en"))) return custom.value(QStringLiteral("en"));
    if (id == QLatin1String("topic:anime")) return i18n("Anime");
    if (id == QLatin1String("topic:manga")) return i18n("Manga");
    if (id == QLatin1String("topic:tv")) return i18n("TV");
    if (id == QLatin1String("topic:movies")) return i18n("Movies");
    if (id == QLatin1String("topic:games")) return i18n("Games");
    if (id == QLatin1String("topic:books")) return i18n("Books");
    if (id == QLatin1String("topic:music")) return i18n("Music");
    if (id == QLatin1String("topic:lyrics")) return i18n("Lyrics");
    if (id == QLatin1String("topic:podcasts")) return i18n("Podcasts");
    if (id == QLatin1String("topic:audiobooks")) return i18n("Audiobooks");
    if (id == QLatin1String("topic:reference")) return i18n("Reference");
    if (id == QLatin1String("topic:software")) return i18n("Software");
    if (id == QLatin1String("audience:general")) return i18n("General");
    if (id == QLatin1String("platform:pc")) return i18n("PC");
    if (id == QLatin1String("requirement:login")) return i18n("Requires login");
    if (id == QLatin1String("requirement:subscription")) return i18n("Requires subscription");
    if (id == QLatin1String("requirement:territory")) return i18n("Territory conditions apply");
    if (id == QLatin1String("requirement:language")) return i18n("Language conditions apply");
    if (id == QLatin1String("requirement:terms")) return i18n("Additional terms apply");
    const auto codeValue = id.section(QLatin1Char(':'), 1);
    if (id.startsWith(QLatin1String("currency:"))) return i18n("%1 pricing", codeValue);
    if (id.startsWith(QLatin1String("region:"))) {
        const auto territory = QLocale::codeToTerritory(codeValue.toUpper());
        return territory == QLocale::AnyTerritory ? i18n("Region: %1", codeValue) : QLocale::territoryToString(territory);
    }
    if (id.startsWith(QLatin1String("topic:"))) return i18n("Topic: %1", codeValue);
    if (id.startsWith(QLatin1String("language:"))) return i18n("Language: %1", codeValue);
    if (id.startsWith(QLatin1String("platform:"))) return i18n("Platform: %1", codeValue);
    if (id.startsWith(QLatin1String("audience:"))) return i18n("Audience: %1", codeValue);
    return id;
}

QString ProviderClassification::description() const
{
    QStringList descriptions;
    for (const auto &id : facets) descriptions.append(label(id));
    for (const auto &id : requirements) descriptions.append(label(id));
    return descriptions.join(QStringLiteral(" · "));
}

bool ProviderClassification::fromJson(const QJsonValue &value, ProviderClassification *result, QString *error)
{
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!value.isObject()) return fail(QStringLiteral("classification must be an object"));
    const auto object = value.toObject();
    const QSet<QString> allowed{QStringLiteral("version"), QStringLiteral("facets"), QStringLiteral("requirements"), QStringLiteral("labels")};
    for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) return fail(QStringLiteral("Unknown classification field"));
    ProviderClassification parsed;
    parsed.version = object.value(QStringLiteral("version")).toString();
    // v1.0.0 defines the complete accepted contract. Future versions require explicit host support.
    if (parsed.version != QLatin1String("1.0.0")) return fail(QStringLiteral("Unsupported classification version (expected 1.0.0)"));
    const QRegularExpression slug(QStringLiteral("^[a-z][a-z0-9-]{0,47}$"));
    const QRegularExpression language(QStringLiteral("^[a-z]{2,3}(-[A-Za-z0-9]{2,8}){0,3}$"));
    const QSet<QString> conditions{QStringLiteral("requirement:login"), QStringLiteral("requirement:subscription"),
        QStringLiteral("requirement:territory"), QStringLiteral("requirement:language"), QStringLiteral("requirement:terms")};
    auto readIds = [&](const QString &key, QStringList *ids, bool requirements) {
        if (!object.contains(key)) return true;
        if (!object.value(key).isArray()) return false;
        const auto array = object.value(key).toArray();
        if (array.size() > 32) return false;
        for (const auto &entry : array) {
            if (!entry.isString()) return false;
            const auto id = entry.toString();
            if (id.size() > 64 || id.count(QLatin1Char(':')) != 1) return false;
            const auto type = id.section(QLatin1Char(':'), 0, 0), code = id.section(QLatin1Char(':'), 1);
            bool valid = false;
            if (requirements) valid = conditions.contains(id);
            else if (type == QLatin1String("topic") || type == QLatin1String("audience") || type == QLatin1String("platform")) valid = slug.match(code).hasMatch();
            else if (type == QLatin1String("currency")) valid = QRegularExpression(QStringLiteral("^[A-Z]{3}$")).match(code).hasMatch();
            else if (type == QLatin1String("region")) valid = QRegularExpression(QStringLiteral("^[a-z]{2}$")).match(code).hasMatch() && QLocale::codeToTerritory(code.toUpper()) != QLocale::AnyTerritory;
            else if (type == QLatin1String("language")) valid = language.match(code).hasMatch();
            if (!valid) return false;
            if (!ids->contains(id)) ids->append(id);
        }
        return true;
    };
    if (!readIds(QStringLiteral("facets"), &parsed.facets, false) || !readIds(QStringLiteral("requirements"), &parsed.requirements, true))
        return fail(QStringLiteral("Invalid, unsupported or excessive provider facets/requirements"));
    if (object.contains(QStringLiteral("labels"))) {
        if (!object.value(QStringLiteral("labels")).isObject()) return fail(QStringLiteral("labels must be an object"));
        const auto labels = object.value(QStringLiteral("labels")).toObject();
        if (labels.size() > 64) return fail(QStringLiteral("Too many classification labels"));
        for (auto it = labels.begin(); it != labels.end(); ++it) {
            if ((!parsed.facets.contains(it.key()) && !parsed.requirements.contains(it.key())) || !it.value().isObject()) return fail(QStringLiteral("Label without matching identifier"));
            const auto translations = it.value().toObject();
            if (translations.size() > 16) return fail(QStringLiteral("Too many label translations"));
            for (auto tr = translations.begin(); tr != translations.end(); ++tr) {
                const auto text = tr.value().toString();
                if (!language.match(tr.key()).hasMatch() || !tr.value().isString() || text.trimmed().isEmpty() || text.size() > 160)
                    return fail(QStringLiteral("Invalid classification translation"));
                for (const auto character : text) if (character.isNull() || character.category() == QChar::Other_Control) return fail(QStringLiteral("Control character in classification label"));
                parsed.labels[it.key()].insert(tr.key(), text);
            }
        }
    }
    *result = parsed;
    if (error) error->clear();
    return true;
}
