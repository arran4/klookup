#pragma once

#include <QString>
#include <Qt>

namespace KLookupRunnerQuery {

// :q: is a KDE help placeholder, not literal text the user should enter.
// Only explicitly prefixed KRunner queries should reach KLookup. Strip
// "lookup " and reject all other input to avoid polluting generic results.
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
    return {};
}

}
