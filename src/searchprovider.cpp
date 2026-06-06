#include "searchprovider.h"
#include <QSettings>

SearchProvider::SearchProvider(QObject *parent)
    : QObject(parent)
{
}

SearchProvider::~SearchProvider()
{
}

bool SearchProvider::isEnabled() const
{
    QSettings settings("KDE", "klookup");
    return settings.value(id() + QStringLiteral("_Enabled"), true).toBool();
}

void SearchProvider::setEnabled(bool enabled)
{
    m_enabled = enabled;
    QSettings settings("KDE", "klookup");
    settings.setValue(id() + QStringLiteral("_Enabled"), enabled);
}
