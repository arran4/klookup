// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "displaypreferences.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QLocale>
#include <QSet>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QSignalBlocker>
#include <cmath>
using namespace DisplayPreferences;
namespace {
QList<QPointer<Context>> contexts;
QString kindName(Kind kind) {
    switch (kind) { case Kind::Boolean: return QStringLiteral("boolean"); case Kind::Enum: return QStringLiteral("enum");
    case Kind::OrderedList: return QStringLiteral("ordered-list"); case Kind::Range: return QStringLiteral("range"); }
    return {};
}
QVariant normalized(const Descriptor &descriptor, const QVariant &value) {
    if (descriptor.kind == Kind::Range) return value.toInt();
    if (descriptor.kind == Kind::OrderedList) return value.toStringList();
    return value;
}
void notify(QString provider, QString key, bool global) {
    const auto peers = contexts;
    for (const auto &peer : peers) if (peer) {
        const bool matchingProvider = peer->property("displayProviderId").toString() == provider;
        for (const auto &descriptor : peer->descriptors()) if (descriptor.key == key && (matchingProvider || (global && descriptor.global))) { emit peer->changed(key); break; }
    }
}
}
QString Descriptor::localizedLabel() const {
    const auto code = QLocale().bcp47Name();
    return labels.value(code, labels.value(code.section(QLatin1Char('-'), 0, 0), labels.value(QStringLiteral("en"), label)));
}
bool Descriptor::accepts(const QVariant &value) const {
    if (kind == Kind::Boolean) return value.metaType().id() == QMetaType::Bool;
    if (kind == Kind::Enum) return value.metaType().id() == QMetaType::QString && choices.contains(value.toString());
    if (kind == Kind::Range) {
        const auto type = value.metaType().id();
        if (type != QMetaType::Int && type != QMetaType::LongLong && type != QMetaType::Double) return false;
        const double number = value.toDouble(); return std::isfinite(number) && std::floor(number) == number && number >= minimum && number <= maximum;
    }
    if (value.metaType().id() != QMetaType::QStringList && value.metaType().id() != QMetaType::QVariantList) return false;
    const auto values = value.toList();
    if (values.size() > 32) return false;
    QSet<QString> seen;
    for (const auto &entry : values) {
        if (entry.metaType().id() != QMetaType::QString || !choices.contains(entry.toString()) || seen.contains(entry.toString())) return false;
        seen.insert(entry.toString());
    }
    return true;
}
bool DisplayPreferences::descriptorsFromJson(const QJsonValue &value, QList<Descriptor> *result, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!value.isArray() || value.toArray().size() > 32) return fail(QStringLiteral("displayOptions must be an array of at most 32 descriptors"));
    QList<Descriptor> parsed; QSet<QString> keys;
    const QSet<QString> fields{QStringLiteral("version"), QStringLiteral("key"), QStringLiteral("label"), QStringLiteral("labels"), QStringLiteral("type"), QStringLiteral("default"), QStringLiteral("choices"), QStringLiteral("minimum"), QStringLiteral("maximum"), QStringLiteral("global"), QStringLiteral("requiresRefresh")};
    for (const auto &entry : value.toArray()) {
        if (!entry.isObject()) return fail(QStringLiteral("Invalid display option"));
        const auto object = entry.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) if (!fields.contains(it.key())) return fail(QStringLiteral("Unknown display option field"));
        Descriptor descriptor; descriptor.version = object.value(QStringLiteral("version")).toString(); descriptor.key = object.value(QStringLiteral("key")).toString(); descriptor.label = object.value(QStringLiteral("label")).toString();
        if (descriptor.version != QLatin1String("1.0.0") || descriptor.key.size() > 96 || !QRegularExpression(QStringLiteral("^[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*){2,8}$")).match(descriptor.key).hasMatch() || keys.contains(descriptor.key)) return fail(QStringLiteral("Unsupported version or invalid/duplicate display key"));
        if (descriptor.label.trimmed().isEmpty() || descriptor.label.size() > 160 || descriptor.label.contains(QChar::Null)) return fail(QStringLiteral("Invalid display label"));
        if (object.contains(QStringLiteral("labels"))) {
            if (!object.value(QStringLiteral("labels")).isObject()) return fail(QStringLiteral("Invalid display translations"));
            const auto labels = object.value(QStringLiteral("labels")).toObject(); if (labels.size() > 16) return fail(QStringLiteral("Too many display translations"));
            for (auto it = labels.begin(); it != labels.end(); ++it) {
                if (!QRegularExpression(QStringLiteral("^[a-z]{2,3}(-[A-Za-z0-9]{2,8}){0,3}$")).match(it.key()).hasMatch() || !it.value().isString() || it.value().toString().trimmed().isEmpty() || it.value().toString().size() > 160 || it.value().toString().contains(QChar::Null)) return fail(QStringLiteral("Invalid display translation"));
                descriptor.labels.insert(it.key(), it.value().toString());
            }
        }
        const auto type = object.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("boolean")) descriptor.kind = Kind::Boolean;
        else if (type == QLatin1String("enum")) descriptor.kind = Kind::Enum;
        else if (type == QLatin1String("ordered-list")) descriptor.kind = Kind::OrderedList;
        else if (type == QLatin1String("range")) descriptor.kind = Kind::Range;
        else return fail(QStringLiteral("Unsupported display type"));
        if (descriptor.kind == Kind::Enum || descriptor.kind == Kind::OrderedList) {
            if (!object.value(QStringLiteral("choices")).isArray() || object.value(QStringLiteral("choices")).toArray().isEmpty() || object.value(QStringLiteral("choices")).toArray().size() > 32) return fail(QStringLiteral("Invalid display choices"));
            for (const auto &choice : object.value(QStringLiteral("choices")).toArray()) {
                const auto id = choice.toString();
                if (!choice.isString() || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$")).match(id).hasMatch() || descriptor.choices.contains(id)) return fail(QStringLiteral("Invalid display choice identifier"));
                descriptor.choices.append(id);
            }
        }
        if (descriptor.kind == Kind::Range) {
            const auto min = object.value(QStringLiteral("minimum")), max = object.value(QStringLiteral("maximum"));
            if (!min.isDouble() || !max.isDouble() || min.toDouble() != min.toInt() || max.toDouble() != max.toInt() || min.toInt() < -1000000 || max.toInt() > 1000000 || min.toInt() > max.toInt()) return fail(QStringLiteral("Invalid display range"));
            descriptor.minimum = min.toInt(); descriptor.maximum = max.toInt();
        }
        for (const auto &flag : {QStringLiteral("global"), QStringLiteral("requiresRefresh")}) if (object.contains(flag) && !object.value(flag).isBool()) return fail(QStringLiteral("Invalid display flag"));
        descriptor.global = object.value(QStringLiteral("global")).toBool(); descriptor.requiresRefresh = object.value(QStringLiteral("requiresRefresh")).toBool();
        descriptor.defaultValue = object.value(QStringLiteral("default")).toVariant();
        if (!descriptor.accepts(descriptor.defaultValue)) return fail(QStringLiteral("Invalid display default"));
        descriptor.defaultValue = normalized(descriptor, descriptor.defaultValue); keys.insert(descriptor.key); parsed.append(descriptor);
    }
    *result = parsed; if (error) error->clear(); return true;
}
Context::Context(const QString &providerId, const QList<Descriptor> &descriptors, QObject *parent) : QObject(parent), m_providerId(providerId) {
    setProperty("displayProviderId", providerId);
    QSet<QString> keys;
    for (const auto &descriptor : descriptors) {
        QJsonObject labels; for (auto it = descriptor.labels.begin(); it != descriptor.labels.end(); ++it) labels.insert(it.key(), it.value());
        QJsonObject object{{QStringLiteral("version"), descriptor.version}, {QStringLiteral("key"), descriptor.key}, {QStringLiteral("label"), descriptor.label},
            {QStringLiteral("labels"), labels}, {QStringLiteral("type"), kindName(descriptor.kind)}, {QStringLiteral("default"), QJsonValue::fromVariant(descriptor.defaultValue)},
            {QStringLiteral("choices"), QJsonArray::fromStringList(descriptor.choices)}, {QStringLiteral("minimum"), descriptor.minimum}, {QStringLiteral("maximum"), descriptor.maximum},
            {QStringLiteral("global"), descriptor.global}, {QStringLiteral("requiresRefresh"), descriptor.requiresRefresh}};
        QList<Descriptor> validated; QString error;
        if (!keys.contains(descriptor.key) && descriptorsFromJson(QJsonArray{object}, &validated, &error)) { m_descriptors.append(validated.first()); keys.insert(descriptor.key); }
        if (m_descriptors.size() == 32) break;
    }
    contexts.removeIf([](const auto &context) { return context.isNull(); }); contexts.append(this);
}
const Descriptor *Context::find(const QString &key) const { for (const auto &descriptor : m_descriptors) if (descriptor.key == key) return &descriptor; return nullptr; }
QString Context::groupName(bool global) const { return global ? QStringLiteral("DisplayGlobal") : QStringLiteral("DisplayProvider-") + QString::fromLatin1(m_providerId.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)); }
QVariant Context::saved(const Descriptor &descriptor, bool global) const {
    if (global && !descriptor.global) return {};
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), groupName(global));
    const auto object = QJsonDocument::fromJson(group.readEntry(descriptor.key, QByteArray())).object();
    const auto value = object.value(QStringLiteral("value")).toVariant();
    return object.value(QStringLiteral("version")).toString() == QLatin1String("1.0.0") && object.value(QStringLiteral("type")).toString() == kindName(descriptor.kind) && descriptor.accepts(value) ? normalized(descriptor, value) : QVariant();
}
QVariant Context::effective(const QString &key) const {
    const auto *descriptor = find(key); if (!descriptor) return {};
    if (m_temporary.contains(key)) return m_temporary.value(key);
    const auto provider = saved(*descriptor, false); if (provider.isValid()) return provider;
    const auto global = saved(*descriptor, true); return global.isValid() ? global : descriptor->defaultValue;
}
QString Context::origin(const QString &key) const {
    const auto *descriptor = find(key); if (!descriptor) return {};
    if (m_temporary.contains(key)) return i18n("Temporary view override");
    if (saved(*descriptor, false).isValid()) return i18n("Provider default");
    if (saved(*descriptor, true).isValid()) return i18n("Global default");
    return i18n("Provider descriptor default");
}
bool Context::setTemporary(const QString &key, const QVariant &value) {
    const auto *descriptor = find(key); if (!descriptor || !descriptor->accepts(value)) return false;
    m_temporary.insert(key, normalized(*descriptor, value)); emit changed(QString(key)); return true;
}
void Context::resetTemporary(const QString &key) { if (m_temporary.remove(key)) emit changed(QString(key)); }
bool Context::write(const QString &key, bool global, const std::optional<QVariant> &value) {
    const auto *descriptor = find(key); if (!descriptor || m_providerId.isEmpty() || m_providerId.size() > 128 || (global && !descriptor->global) || (value && !descriptor->accepts(*value))) return false;
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), groupName(global));
    LastWrite change; change.group = groupName(global); change.key = key; change.before = group.hasKey(key); change.oldBytes = group.readEntry(key, QByteArray()); change.previousTemporary = m_temporary;
    change.after = value.has_value();
    if (value) {
        change.newBytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("type"), kindName(descriptor->kind)}, {QStringLiteral("value"), QJsonValue::fromVariant(normalized(*descriptor, *value))}}).toJson(QJsonDocument::Compact);
        group.writeEntry(key, change.newBytes);
        m_temporary.remove(key);
        // Promoting a global choice must not erase a saved provider override or change this view unexpectedly.
        if (effective(key) != normalized(*descriptor, *value)) m_temporary.insert(key, normalized(*descriptor, *value));
    } else group.deleteEntry(key);
    group.sync(); m_last = change; notify(m_providerId, key, global); return true;
}
bool Context::saveDefault(const QString &key, bool global) { return write(key, global, effective(key)); }
bool Context::clearDefault(const QString &key, bool global) { return write(key, global, {}); }
bool Context::canUndo() const {
    if (!m_last) return false;
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), m_last->group);
    return group.hasKey(m_last->key) == m_last->after && (!m_last->after || group.readEntry(m_last->key, QByteArray()) == m_last->newBytes);
}
bool Context::undo() {
    if (!canUndo()) return false;
    const auto change = *m_last; KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), change.group);
    if (change.before) group.writeEntry(change.key, change.oldBytes); else group.deleteEntry(change.key);
    group.sync(); m_temporary = change.previousTemporary; m_last.reset(); notify(m_providerId, change.key, change.group == groupName(true)); return true;
}
QDialog *Context::createEditor(QWidget *parent) {
    auto *dialog = new QDialog(parent); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(i18n("View display options")); dialog->setObjectName(QStringLiteral("displayOptionsDialog")); dialog->resize(650, 500);
    auto *layout = new QVBoxLayout(dialog); auto *scroll = new QScrollArea(dialog); scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll); auto *form = new QVBoxLayout(content); scroll->setWidget(content); layout->addWidget(scroll);
    QPointer<Context> model(this);
    for (const auto &spec : m_descriptors) {
        auto *label = new QLabel(spec.localizedLabel(), content); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); form->addWidget(label);
        QWidget *control = nullptr;
        if (spec.kind == Kind::Boolean) { auto *box = new QCheckBox(spec.localizedLabel(), content); control = box; connect(box, &QCheckBox::toggled, dialog, [model, spec](bool value) { if (model) model->setTemporary(spec.key, value); }); }
        else if (spec.kind == Kind::Enum) { auto *combo = new QComboBox(content); for (const auto &choice : spec.choices) combo->addItem(choice, choice); control = combo; connect(combo, &QComboBox::currentIndexChanged, dialog, [model, spec, combo](int) { if (model) model->setTemporary(spec.key, combo->currentData()); }); }
        else if (spec.kind == Kind::Range) { auto *spin = new QSpinBox(content); spin->setRange(spec.minimum, spec.maximum); control = spin; connect(spin, &QSpinBox::valueChanged, dialog, [model, spec](int value) { if (model) model->setTemporary(spec.key, value); }); }
        else {
            auto *list = new QListWidget(content); control = list;
            for (const auto &choice : spec.choices) { auto *item = new QListWidgetItem(choice, list); item->setData(Qt::UserRole, choice); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Unchecked); }
            auto apply = [model, spec, list] { QStringList values; for (int row = 0; row < list->count(); ++row) if (list->item(row)->checkState() == Qt::Checked) values.append(list->item(row)->data(Qt::UserRole).toString()); if (model) model->setTemporary(spec.key, values); };
            connect(list, &QListWidget::itemChanged, dialog, [apply](QListWidgetItem *) { apply(); });
            auto *order = new QHBoxLayout;
            for (int step : {-1, 1}) { auto *button = new QPushButton(step < 0 ? i18n("Move up") : i18n("Move down"), content); order->addWidget(button); connect(button, &QPushButton::clicked, dialog, [list, step, apply] { const int row = list->currentRow(); if (row < 0 || row + step < 0 || row + step >= list->count()) return; auto *item = list->takeItem(row); list->insertItem(row + step, item); list->setCurrentItem(item); apply(); }); }
            form->addLayout(order);
        }
        control->setObjectName(QStringLiteral("displayOption-") + spec.key); control->setAccessibleName(spec.localizedLabel()); form->addWidget(control);
        auto *origin = new QLabel(content); origin->setTextFormat(Qt::PlainText); origin->setObjectName(QStringLiteral("displayOrigin-") + spec.key); form->addWidget(origin);
        if (spec.requiresRefresh) { auto *notice = new QLabel(i18n("Changing this option requires an explicit Refresh. No request starts here."), content); notice->setWordWrap(true); form->addWidget(notice); }
        auto update = [model, spec, control, origin] {
            if (!model) return;
            const auto value = model->effective(spec.key); const QSignalBlocker block(control);
            if (auto *box = qobject_cast<QCheckBox *>(control)) box->setChecked(value.toBool());
            else if (auto *combo = qobject_cast<QComboBox *>(control)) combo->setCurrentIndex(combo->findData(value));
            else if (auto *spin = qobject_cast<QSpinBox *>(control)) spin->setValue(value.toInt());
            else if (auto *list = qobject_cast<QListWidget *>(control)) {
                auto order = value.toStringList(); for (const auto &choice : spec.choices) if (!order.contains(choice)) order.append(choice);
                for (int target = 0; target < order.size(); ++target) for (int row = target; row < list->count(); ++row) if (list->item(row)->data(Qt::UserRole).toString() == order[target]) { auto *item = list->takeItem(row); list->insertItem(target, item); item->setCheckState(value.toStringList().contains(order[target]) ? Qt::Checked : Qt::Unchecked); break; }
            }
            origin->setText(model->origin(spec.key));
        };
        connect(this, &Context::changed, dialog, [update, spec](const QString &key) { if (key == spec.key) update(); }); update();
        auto *actions = new QHBoxLayout;
        auto *provider = new QPushButton(i18n("Save provider default"), content); provider->setObjectName(QStringLiteral("saveProvider-") + spec.key); actions->addWidget(provider);
        connect(provider, &QPushButton::clicked, dialog, [model, spec] { if (model) model->saveDefault(spec.key, false); });
        if (spec.global) { auto *global = new QPushButton(i18n("Save global default"), content); global->setObjectName(QStringLiteral("saveGlobal-") + spec.key); actions->addWidget(global); connect(global, &QPushButton::clicked, dialog, [model, spec] { if (model) model->saveDefault(spec.key, true); }); }
        auto *reset = new QToolButton(content); reset->setText(i18n("Reset…")); reset->setPopupMode(QToolButton::InstantPopup); auto *menu = new QMenu(reset);
        auto *view = menu->addAction(i18n("Use inherited value in this view")); connect(view, &QAction::triggered, dialog, [model, spec] { if (model) model->resetTemporary(spec.key); });
        auto *clearProvider = menu->addAction(i18n("Clear saved provider default")); connect(clearProvider, &QAction::triggered, dialog, [model, spec] { if (model) model->clearDefault(spec.key, false); });
        if (spec.global) { auto *clearGlobal = menu->addAction(i18n("Clear saved global default")); connect(clearGlobal, &QAction::triggered, dialog, [model, spec] { if (model) model->clearDefault(spec.key, true); }); }
        reset->setMenu(menu); actions->addWidget(reset); form->addLayout(actions);
    }
    form->addStretch();
    auto *undo = new QPushButton(i18n("Undo last saved-default change"), dialog); undo->setObjectName(QStringLiteral("undoDisplayDefault")); undo->setEnabled(canUndo()); layout->addWidget(undo);
    connect(undo, &QPushButton::clicked, dialog, [model] { if (model) model->undo(); });
    connect(this, &Context::changed, dialog, [model, undo](const QString &) { undo->setEnabled(model && model->canUndo()); });
    connect(this, &QObject::destroyed, dialog, [dialog] { dialog->setEnabled(false); dialog->deleteLater(); });
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog); layout->addWidget(close); connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    return dialog;
}
