#ifndef KLOOKUP_RUNNER_H
#define KLOOKUP_RUNNER_H

// #include <KRunner/AbstractRunner>
#include <QObject>
#include <QString>
#include <QList>
#include <QVariant>

namespace KRunner {
    class RunnerContext {
    public:
        QString query() const { return QString(); }
        void addMatches(const QList<QVariant> &matches) {}
    };

    class AbstractRunner : public QObject {
        Q_OBJECT
    public:
        explicit AbstractRunner(QObject *parent, const QVariantList &args) : QObject(parent) {}
        virtual void match(RunnerContext &context) {}
        virtual void run(const RunnerContext &context, const QVariant &match) {}
    };
}

class KLookupRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    explicit KLookupRunner(QObject *parent, const QVariantList &args);
    ~KLookupRunner() override;

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const QVariant &match) override;
};

#endif // KLOOKUP_RUNNER_H
