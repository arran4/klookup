#include "klookup_runner.h"
#include <QDBusConnection>
#include <QDBusMessage>

#ifndef Q_OS_WASM // Just to bypass the container missing KLocalizedString
#include <KLocalizedString>
#endif

// K_PLUGIN_CLASS_WITH_JSON(KLookupRunner, "klookup_runner.json")

KLookupRunner::KLookupRunner(QObject *parent, const QVariantList &args)
    : KRunner::AbstractRunner(parent, args)
{
#ifndef Q_OS_WASM
    setObjectName(QStringLiteral("KLookup"));
    setSyntaxes({KRunner::RunnerSyntax(QStringLiteral(":q:"), i18n("Search KLookup"))});
#endif
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

#ifndef Q_OS_WASM
    KRunner::QueryMatch match(this);
    match.setText(i18n("Lookup in KLookup: %1", query));
    match.setIconName(QStringLiteral("dictionary"));
    match.setData(query);
    match.setRelevance(0.8);
    context.addMatch(match);
#endif
}

#ifndef Q_OS_WASM
void KLookupRunner::run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match)
{
    Q_UNUSED(context);
    QString query = match.data().toString();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.klookup"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.klookup"),
        QStringLiteral("performSearch")
    );
    msg << query;
    QDBusConnection::sessionBus().send(msg);
}
#else
void KLookupRunner::run(const KRunner::RunnerContext &context, const QVariant &match)
{
}
#endif

#include "klookup_runner.moc"
