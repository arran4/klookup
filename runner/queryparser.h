#pragma once

#include <QString>
#include <Qt>

namespace KLookupRunnerQuery {

// :q: is a KDE help placeholder, not literal text the user should enter.
// Keep existing unprefixed searches working. When the explicit "lookup "
// prefix is used, strip it before handing the query to the application.
inline QString searchTerm(const QString &raw)
{
    const QString input = raw.trimmed();
    if (input.compare(QStringLiteral("lookup"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    if (input.startsWith(QStringLiteral("lookup "), Qt::CaseInsensitive)) {
        const QString value = input.mid(7).trimmed();
        return value.size() >= 3 ? value : QString{};
    }
    return input.size() >= 3 ? input : QString{};
}

}
