#include "providerdefaults.h"
#include "searchprovider.h"

SearchProvider::SearchProvider(QObject *parent)
    : QObject(parent)
{
}

SearchProvider::~SearchProvider()
{
}

bool SearchProvider::isEnabled() const
{
    return m_enabled.value_or(ProviderDefaults::enabled(id()));
}

void SearchProvider::setEnabled(bool enabled)
{
    m_enabled = enabled;
}
