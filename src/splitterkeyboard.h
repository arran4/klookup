// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SPLITTERKEYBOARD_H
#define SPLITTERKEYBOARD_H
#include <QKeyEvent>
#include <QSplitter>

inline bool resizeSplitterWithKey(QSplitter *splitter, QKeyEvent *event)
{
    const bool horizontal = splitter->orientation() == Qt::Horizontal;
    const int negative = horizontal ? Qt::Key_Left : Qt::Key_Up;
    const int positive = horizontal ? Qt::Key_Right : Qt::Key_Down;
    if (event->key() != negative && event->key() != positive) return false;
    const auto sizes = splitter->sizes();
    if (sizes.size() != 2) return false;
    int delta = event->key() == positive ? 10 : -10;
    if (horizontal && splitter->layoutDirection() == Qt::RightToLeft) delta = -delta;
    const int total = sizes[0] + sizes[1];
    const int first = qBound(0, sizes[0] + delta, total);
    splitter->setSizes({first, total - first});
    emit splitter->splitterMoved(splitter->sizes()[0], 1);
    event->accept();
    return true;
}
#endif
