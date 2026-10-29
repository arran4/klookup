// SPDX-FileCopyrightText: 2026 KLookup contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AUTOSTARTMANAGER_H
#define AUTOSTARTMANAGER_H

#include <QString>

class AutostartManager
{
public:
    explicit AutostartManager(const QString &customAutostartDir = QString());
    virtual ~AutostartManager() = default;

    bool isAutostartEnabled() const;
    bool setAutostartEnabled(bool enabled);

    QString autostartDirectory() const;
    QString autostartFilePath() const;

private:
    QString m_customDir;
};

#endif // AUTOSTARTMANAGER_H
