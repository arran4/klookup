#ifndef KLOOKUP_RUNNER_H
#define KLOOKUP_RUNNER_H

#include <KRunner/AbstractRunner>
#include <QObject>
#include <QString>
#include <QList>
#include <QVariant>

class KLookupRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    explicit KLookupRunner(QObject *parent, const KPluginMetaData &metaData);
    ~KLookupRunner() override;

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;
};

#endif // KLOOKUP_RUNNER_H
