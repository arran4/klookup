// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LANGUAGEPREFERENCES_H
#define LANGUAGEPREFERENCES_H
#include <QLocale>
#include <QStringList>
#include <QWidget>
#include <QVariant>
class KConfigGroup;
class QListWidget;
class QLineEdit;
namespace Languages {
struct Title { QString language; QString text; bool original = false; };
struct Selection { Title preferred; Title original; QList<Title> alternatives; };
struct Summary { Title preferred; Title original; Title uiLocaleTitle; int omittedCount = 0; };
QString canonicalTag(QString code);
QString providerCode(const QString &providerId, QString code);
QString label(const QString &code);
QStringList normalized(const QStringList &codes);
QStringList defaults(const QLocale &uiLocale = QLocale());
QStringList systemSuggestions(const QLocale &uiLocale = QLocale());
QStringList read(const KConfigGroup &settings);
QList<Title> fromData(const QVariant &value, const QString &providerId = {});
QVariantList toData(const QList<Title> &titles);
void showAllTitles(const QList<Title> &titles, QWidget *parent);
Selection select(const QList<Title> &titles, const QStringList &preferences, const QString &originalLanguage = {});
Summary summarizeTitles(const QList<Title> &titles, const QStringList &preferences, const QString &originalLanguage = {}, const QLocale &uiLocale = QLocale());
}
class QHBoxLayout;
class LanguagePreferencesWidget : public QWidget {
public:
    explicit LanguagePreferencesWidget(QWidget *parent = nullptr);
    void setUiLocale(const QLocale &uiLocale);
    void saveSettings();
private:
    void setCodes(const QStringList &codes);
    void rebuildSuggestions();
    QStringList codes() const;
    QLocale m_uiLocale;
    QListWidget *m_list;
    QLineEdit *m_add;
    QHBoxLayout *m_suggestionsLayout = nullptr;
    bool m_inherit = true;
};
#endif
