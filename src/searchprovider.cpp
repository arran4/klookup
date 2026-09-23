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
    return m_enabled;
}

void SearchProvider::setEnabled(bool enabled)
{
    m_enabled = enabled;
}
