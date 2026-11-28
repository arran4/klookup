// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef KLOOKUP_TESTHELPERS_H
#define KLOOKUP_TESTHELPERS_H

#include "searchprovider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

class TestNullReply : public QNetworkReply
{
public:
    explicit TestNullReply(const QNetworkRequest &request, QObject *parent = nullptr)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit finished();
        });
    }
    void abort() override { setFinished(true); }

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class TestCountingNetworkManager : public QNetworkAccessManager
{
public:
    explicit TestCountingNetworkManager(QObject *parent = nullptr)
        : QNetworkAccessManager(parent)
    {
    }
    int requests = 0;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        ++requests;
        return new TestNullReply(request, this);
    }
};

#endif // KLOOKUP_TESTHELPERS_H
