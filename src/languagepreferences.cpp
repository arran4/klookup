// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "languagepreferences.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QRegularExpression>
#include <QSet>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QHeaderView>

QString Languages::canonicalTag(QString code)
{
    code = code.trimmed(); code.replace(QLatin1Char('_'), QLatin1Char('-'));
    if (code.isEmpty() || code.size() > 64) return {};
    auto parts = code.split(QLatin1Char('-'));
    if (parts.size() > 16) return {};
    static const QRegularExpression alpha(QStringLiteral("^[A-Za-z]+$")), alnum(QStringLiteral("^[A-Za-z0-9]+$"));
    if (!alpha.match(parts[0]).hasMatch() || parts[0].size() < 2 || parts[0].size() > 8) return {};
    static const QMap<QString, QString> aliases{{QStringLiteral("eng"), QStringLiteral("en")}, {QStringLiteral("jpn"), QStringLiteral("ja")},
        {QStringLiteral("fra"), QStringLiteral("fr")}, {QStringLiteral("fre"), QStringLiteral("fr")},
        {QStringLiteral("deu"), QStringLiteral("de")}, {QStringLiteral("ger"), QStringLiteral("de")},
        {QStringLiteral("zho"), QStringLiteral("zh")}, {QStringLiteral("chi"), QStringLiteral("zh")},
        {QStringLiteral("kor"), QStringLiteral("ko")}, {QStringLiteral("spa"), QStringLiteral("es")},
        {QStringLiteral("por"), QStringLiteral("pt")}, {QStringLiteral("ita"), QStringLiteral("it")},
        {QStringLiteral("rus"), QStringLiteral("ru")}, {QStringLiteral("ara"), QStringLiteral("ar")},
        {QStringLiteral("heb"), QStringLiteral("he")}, {QStringLiteral("iw"), QStringLiteral("he")},
        {QStringLiteral("in"), QStringLiteral("id")}, {QStringLiteral("ji"), QStringLiteral("yi")}};
    parts[0] = aliases.value(parts[0].toLower(), parts[0].toLower());
    int index = 1, extlangs = 0;
    // RFC 5646 structural validation. Unknown valid codes remain codes, never guessed languages.
    while (index < parts.size() && parts[index].size() == 3 && alpha.match(parts[index]).hasMatch() && extlangs < 3 && parts[0].size() <= 3) {
        parts[index] = parts[index].toLower(); ++index; ++extlangs;
    }
    if (index < parts.size() && parts[index].size() == 4 && alpha.match(parts[index]).hasMatch()) {
        parts[index] = parts[index].toLower(); parts[index][0] = parts[index][0].toUpper(); ++index;
    }
    if (index < parts.size() && ((parts[index].size() == 2 && alpha.match(parts[index]).hasMatch()) ||
        QRegularExpression(QStringLiteral("^[0-9]{3}$")).match(parts[index]).hasMatch())) { parts[index] = parts[index].toUpper(); ++index; }
    QSet<QString> variants, extensions;
    while (index < parts.size() && parts[index].size() >= 4) {
        auto part = parts[index].toLower();
        if (!alnum.match(part).hasMatch() || part.size() > 8 || (part.size() == 4 && !part[0].isDigit()) || variants.contains(part)) return {};
        variants.insert(part); parts[index++] = part;
    }
    while (index < parts.size()) {
        auto singleton = parts[index].toLower();
        if (singleton.size() != 1 || !alnum.match(singleton).hasMatch() || extensions.contains(singleton)) return {};
        extensions.insert(singleton); parts[index++] = singleton;
        const int start = index;
        while (index < parts.size() && (singleton == QLatin1String("x") || parts[index].size() > 1)) {
            const auto part = parts[index].toLower();
            if (part.isEmpty() || part.size() > 8 || !alnum.match(part).hasMatch() || (singleton != QLatin1String("x") && part.size() < 2)) return {};
            parts[index++] = part;
        }
        if (index == start) return {};
    }
    return parts.join(QLatin1Char('-'));
}

QString Languages::providerCode(const QString &providerId, QString code)
{
    // Open Library's language identifiers are path-shaped ISO 639 codes.
    if (providerId == QLatin1String("openlibrary") && code.startsWith(QLatin1String("/languages/"))) code.remove(0, 11);
    return canonicalTag(code);
}
QString Languages::label(const QString &code)
{
    const auto canonical = canonicalTag(code);
    if (canonical.isEmpty()) return {};
    const QLocale locale(canonical);
    return locale.language() == QLocale::C ? canonical : i18n("%1 (%2)", locale.nativeLanguageName(), canonical);
}
QStringList Languages::normalized(const QStringList &codes)
{
    QStringList result;
    for (const auto &code : codes) {
        const auto tag = canonicalTag(code);
        if (!tag.isEmpty() && !result.contains(tag)) result.append(tag);
        if (result.size() == 16) break;
    }
    return result;
}
QStringList Languages::defaults(const QLocale &uiLocale)
{
    const auto tag = canonicalTag(uiLocale.bcp47Name());
    return tag.isEmpty() || tag.section(QLatin1Char('-'), 0, 0) == QLatin1String("en") ? QStringList{QStringLiteral("en")} : normalized({tag, QStringLiteral("en")});
}
QStringList Languages::read(const KConfigGroup &settings)
{
    return settings.hasKey(QStringLiteral("Preferred")) ? normalized(settings.readEntry(QStringLiteral("Preferred"), QStringList())) : defaults();
}
Languages::Selection Languages::select(const QList<Title> &titles, const QStringList &preferences, const QString &originalLanguage)
{
    Selection result; QList<Title> available;
    const auto originalCode = canonicalTag(originalLanguage);
    for (const auto &title : titles) {
        if (title.text.trimmed().isEmpty() || title.text.size() > 32768 || available.size() == 128) continue;
        Title value{canonicalTag(title.language), title.text, title.original};
        available.append(value);
        if (result.original.text.isEmpty() && (value.original || (!originalCode.isEmpty() && value.language == originalCode))) result.original = value;
    }
    for (const auto &preferred : normalized(preferences)) {
        for (const auto &title : available) if (title.language == preferred) { result.preferred = title; break; }
        if (!result.preferred.text.isEmpty()) break;
        // Base-language fallback cannot cross an explicitly requested script.
        const auto primary = preferred.section(QLatin1Char('-'), 0, 0);
        const auto script = QRegularExpression(QStringLiteral("(?:^|-)([A-Z][a-z]{3})(?:-|$)")).match(preferred).captured(1);
        for (const auto &title : available) {
            if (title.language.section(QLatin1Char('-'), 0, 0) != primary) continue;
            if (!script.isEmpty() && !title.language.split(QLatin1Char('-')).contains(script)) continue;
            result.preferred = title; break;
        }
        if (!result.preferred.text.isEmpty()) break;
    }
    if (result.preferred.text.isEmpty()) result.preferred = !result.original.text.isEmpty() ? result.original : available.value(0);
    for (const auto &title : available) if (title.text != result.preferred.text) result.alternatives.append(title);
    return result;
}

LanguagePreferencesWidget::LanguagePreferencesWidget(QWidget *parent) : QWidget(parent), m_list(new QListWidget(this)), m_add(new QLineEdit(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(i18n("Preferred title languages, in order. Source-original titles remain available. An empty list uses the source-original title, then the first supplied title. No translations are generated."), this);
    help->setWordWrap(true); help->setTextFormat(Qt::PlainText); layout->addWidget(help);
    m_list->setObjectName(QStringLiteral("preferredLanguages")); m_list->setAccessibleName(i18n("Ordered preferred languages")); layout->addWidget(m_list, 1);
    m_add->setObjectName(QStringLiteral("languageCode")); m_add->setPlaceholderText(i18n("Language code, e.g. en, ja, zh-Hant or pt-BR")); m_add->setAccessibleName(i18n("BCP 47 language code"));
    auto *add = new QPushButton(i18n("Add language"), this); add->setObjectName(QStringLiteral("addPreferredLanguage"));
    auto *entry = new QHBoxLayout; entry->addWidget(m_add); entry->addWidget(add); layout->addLayout(entry);
    connect(add, &QPushButton::clicked, this, [this] {
        const auto tag = Languages::canonicalTag(m_add->text());
        if (tag.isEmpty() || codes().contains(tag) || m_list->count() >= 16) { m_add->setFocus(); return; }
        auto updated = codes(); updated.append(tag); setCodes(updated); m_add->clear(); m_inherit = false;
    });
    connect(m_add, &QLineEdit::returnPressed, add, &QPushButton::click);
    auto *buttons = new QHBoxLayout;
    for (int action = 0; action < 4; ++action) {
        auto *button = new QPushButton(QStringList{i18n("Move up"), i18n("Move down"), i18n("Remove"), i18n("Use UI-language default")}[action], this);
        button->setObjectName(QStringList{QStringLiteral("languageUp"), QStringLiteral("languageDown"), QStringLiteral("removeLanguage"), QStringLiteral("resetLanguages")}[action]); buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, action] {
            const int row = m_list->currentRow(); auto values = codes();
            if (action == 3) { setCodes(Languages::defaults()); m_inherit = true; return; }
            if (row < 0) return;
            int target = row;
            if (action == 0 && row > 0) { values.swapItemsAt(row, row - 1); --target; }
            else if (action == 1 && row + 1 < values.size()) { values.swapItemsAt(row, row + 1); ++target; }
            else if (action == 2) values.removeAt(row);
            else return;
            setCodes(values); m_list->setCurrentRow(qMin(target, int(values.size()) - 1)); m_inherit = false;
        });
    }
    layout->addLayout(buttons);
    const KConfigGroup settings(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Languages"));
    m_inherit = !settings.hasKey(QStringLiteral("Preferred")); setCodes(Languages::read(settings));
}
void LanguagePreferencesWidget::setCodes(const QStringList &codes)
{
    m_list->clear();
    for (const auto &code : codes) { auto *item = new QListWidgetItem(Languages::label(code), m_list); item->setData(Qt::UserRole, code); }
}
QStringList LanguagePreferencesWidget::codes() const
{
    QStringList result; for (int row = 0; row < m_list->count(); ++row) result.append(m_list->item(row)->data(Qt::UserRole).toString()); return result;
}
void LanguagePreferencesWidget::saveSettings()
{
    KConfigGroup settings(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Languages"));
    if (m_inherit) settings.deleteEntry(QStringLiteral("Preferred")); else settings.writeEntry(QStringLiteral("Preferred"), codes());
    settings.sync();
}

QList<Languages::Title> Languages::fromData(const QVariant &value, const QString &providerId)
{
    QList<Title> titles;
    for (const auto &entry : value.toList()) {
        const auto record = entry.toMap();
        const auto text = record.value(QStringLiteral("title")).toString();
        if (text.trimmed().isEmpty() || text.size() > 32768) continue;
        titles.append({providerCode(providerId, record.value(QStringLiteral("language")).toString()), text,
            record.value(QStringLiteral("original")).metaType().id() == QMetaType::Bool && record.value(QStringLiteral("original")).toBool()});
        if (titles.size() == 128) break;
    }
    return titles;
}
QVariantList Languages::toData(const QList<Title> &titles)
{
    QVariantList result;
    for (const auto &title : titles) {
        if (title.text.trimmed().isEmpty() || title.text.size() > 32768) continue;
        result.append(QVariantMap{{QStringLiteral("language"), canonicalTag(title.language)}, {QStringLiteral("title"), title.text}, {QStringLiteral("original"), title.original}});
        if (result.size() == 128) break;
    }
    return result;
}
void Languages::showAllTitles(const QList<Title> &titles, QWidget *parent)
{
    auto *dialog = new QDialog(parent); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("allTitlesDialog")); dialog->setWindowTitle(i18n("All titles")); dialog->resize(650, 400);
    auto *layout = new QVBoxLayout(dialog);
    auto *list = new QTreeWidget(dialog); list->setObjectName(QStringLiteral("localizedTitleList"));
    list->setHeaderLabels({i18n("Language"), i18n("Title"), i18n("Source-original")});
    list->setAccessibleName(i18n("All supplied titles"));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents); list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (const auto &title : fromData(toData(titles))) new QTreeWidgetItem(list, {label(title.language), title.text, title.original ? i18n("Yes") : QString()});
    layout->addWidget(list);
    auto *copy = new QPushButton(i18n("Copy selected titles"), dialog);
    QObject::connect(copy, &QPushButton::clicked, dialog, [list] {
        QStringList selected;
        for (auto *item : list->selectedItems()) selected.append(item->text(1));
        if (!selected.isEmpty()) QGuiApplication::clipboard()->setText(selected.join(QLatin1Char('\n')));
    });
    layout->addWidget(copy);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog); layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    dialog->show();
}
