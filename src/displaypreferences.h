// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DISPLAYPREFERENCES_H
#define DISPLAYPREFERENCES_H
#include <QObject>
#include <QJsonValue>
#include <QMap>
#include <QPointer>
#include <QVariant>
#include <QStringList>
#include <optional>
class QDialog;
class QWidget;
namespace DisplayPreferences {
enum class Kind { Boolean, Enum, OrderedList, Range };
struct Descriptor {
    QString version = QStringLiteral("1.0.0");
    QString key, label;
    QMap<QString, QString> labels;
    Kind kind = Kind::Boolean;
    QVariant defaultValue = false;
    QStringList choices;
    int minimum = 0, maximum = 100;
    bool global = false;
    bool requiresRefresh = false;
    QString localizedLabel() const;
    bool accepts(const QVariant &value) const;
};
bool descriptorsFromJson(const QJsonValue &value, QList<Descriptor> *result, QString *error);
class Context : public QObject {
    Q_OBJECT
public:
    Context(const QString &providerId, const QList<Descriptor> &descriptors, QObject *parent = nullptr);
    QList<Descriptor> descriptors() const { return m_descriptors; }
    QVariant effective(const QString &key) const;
    QString origin(const QString &key) const;
    QVariantMap temporaryValues() const { return m_temporary; }
    bool setTemporary(const QString &key, const QVariant &value);
    void resetTemporary(const QString &key);
    bool saveDefault(const QString &key, bool global);
    bool clearDefault(const QString &key, bool global);
    bool undo();
    bool canUndo() const;
    QDialog *createEditor(QWidget *parent = nullptr);
signals:
    void changed(const QString &key);
private:
    const Descriptor *find(const QString &key) const;
    QVariant saved(const Descriptor &descriptor, bool global) const;
    QString groupName(bool global) const;
    bool write(const QString &key, bool global, const std::optional<QVariant> &value);
    QString m_providerId;
    QList<Descriptor> m_descriptors;
    QVariantMap m_temporary;
    struct LastWrite { QString group, key; bool before = false, after = false; QByteArray oldBytes, newBytes; QVariantMap previousTemporary; };
    std::optional<LastWrite> m_last;
};
}
Q_DECLARE_METATYPE(DisplayPreferences::Descriptor)
#endif
