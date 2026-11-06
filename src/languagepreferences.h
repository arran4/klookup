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
QString canonicalTag(QString code);
QString providerCode(const QString &providerId, QString code);
QString label(const QString &code);
QStringList normalized(const QStringList &codes);
QStringList defaults(const QLocale &uiLocale = QLocale());
QStringList read(const KConfigGroup &settings);
QList<Title> fromData(const QVariant &value, const QString &providerId = {});
QVariantList toData(const QList<Title> &titles);
void showAllTitles(const QList<Title> &titles, QWidget *parent);
Selection select(const QList<Title> &titles, const QStringList &preferences, const QString &originalLanguage = {});
}
class LanguagePreferencesWidget : public QWidget {
public:
    explicit LanguagePreferencesWidget(QWidget *parent = nullptr);
    void saveSettings();
private:
    void setCodes(const QStringList &codes);
    QStringList codes() const;
    QListWidget *m_list;
    QLineEdit *m_add;
    bool m_inherit = true;
};
#endif
