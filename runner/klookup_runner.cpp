#include "klookup_runner.h"
#include <QDBusConnection>
#include <QDBusMessage>

// K_PLUGIN_CLASS_WITH_JSON(KLookupRunner, "klookup_runner.json")

KLookupRunner::KLookupRunner(QObject *parent, const QVariantList &args)
    : KRunner::AbstractRunner(parent, args)
{
}

KLookupRunner::~KLookupRunner()
{
}

void KLookupRunner::match(KRunner::RunnerContext &context)
{
    QString query = context.query();
    if (query.length() < 3) {
        return;
    }
}

void KLookupRunner::run(const KRunner::RunnerContext &context, const QVariant &match)
{
    Q_UNUSED(context);
    QString query = match.toString();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.klookup"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.klookup"),
        QStringLiteral("performSearch")
    );
    msg << query;
    QDBusConnection::sessionBus().send(msg);
}

#include "klookup_runner.moc"
