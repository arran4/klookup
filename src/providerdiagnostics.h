// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "providerpackage.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>

class SearchProvider;
class JsProvider;
class QPluginLoader;
class QProcess;
class QTimer;

class ProviderDiagnostics : public QObject {
    Q_OBJECT
public:
    enum class Mode { Preflight, Offline, Live };
    struct Source {
        QString id, type, path, version;
        ProviderPackage package;
        QPointer<SearchProvider> native;
        bool enabled = true;
    };
    explicit ProviderDiagnostics(QObject *parent = nullptr);
    ~ProviderDiagnostics() override;
    static QList<Source> discover(const QStringList &packagePaths = {}, const QStringList &nativePaths = {});
    static QList<Source> forSettings(const QList<SearchProvider *> &, const QList<ProviderPackageStatus> &);
    quint64 run(const QList<Source> &, Mode mode = Mode::Preflight);
    void cancel();
    static QJsonObject aggregate(const QList<QJsonObject> &reports);
signals:
    void resultReady(quint64 run, const QJsonObject &result);
    void finished(quint64 run, const QJsonObject &report);
private:
    void next();
    void report(QString status, QString code, QString message, QString remedy = {});
    QList<Source> m_queue;
    Source m_current;
    QList<QJsonObject> m_results;
    Mode m_mode = Mode::Preflight;
    quint64 m_run = 0;
    QElapsedTimer m_elapsed;
    QPointer<JsProvider> m_script;
    QProcess *m_process = nullptr;
    QTimer *m_nativeDeadline = nullptr;
    bool m_hasCurrent = false;
    bool m_running = false;
};
