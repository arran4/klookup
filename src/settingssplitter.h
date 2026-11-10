// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SETTINGSSPLITTER_H
#define SETTINGSSPLITTER_H
#include "splitterkeyboard.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QSplitterHandle>

class SettingsSplitter : public QSplitter {
public:
    SettingsSplitter(const QString &key, const QString &label, QWidget *list, QWidget *details, QWidget *parent)
        : QSplitter(Qt::Vertical, parent), m_key(key)
    {
        setObjectName(key);
        list->setMinimumHeight(100); details->setMinimumHeight(100);
        addWidget(list); addWidget(details);
        setChildrenCollapsible(false);
        setStretchFactor(0, 1); setStretchFactor(1, 1);
        auto *separator = handle(1);
        separator->setFocusPolicy(Qt::StrongFocus);
        separator->setAccessibleName(label);
        separator->setAccessibleDescription(i18n("Use up and down arrow keys to resize. Press Enter or double-click to reset."));
        separator->installEventFilter(this);
        const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
        auto sizes = group.readEntry(key, QList<int>{280, 280});
        if (sizes.size() != 2 || sizes[0] <= 0 || sizes[1] <= 0) sizes = {280, 280};
        setSizes(sizes);
        connect(this, &QSplitter::splitterMoved, this, [this] {
            const auto relativeSizes = this->sizes();
            if (relativeSizes.size() != 2 || relativeSizes[0] <= 0 || relativeSizes[1] <= 0) return;
            KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
            group.writeEntry(m_key, relativeSizes); group.sync();
        });
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == handle(1)) {
            if (event->type() == QEvent::KeyPress) {
                auto *key = static_cast<QKeyEvent *>(event);
                if (resizeSplitterWithKey(this, key)) return true;
                if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space) { reset(); return true; }
            }
            if (event->type() == QEvent::MouseButtonDblClick) { reset(); return true; }
        }
        return QSplitter::eventFilter(watched, event);
    }
private:
    void reset() { setSizes({280, 280}); emit splitterMoved(sizes()[0], 1); }
    QString m_key;
};
#endif
