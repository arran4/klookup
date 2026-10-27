#ifndef SEARCHCONTINUATION_H
#define SEARCHCONTINUATION_H

#include "searchprovider.h"

// Optional interface: existing SearchProvider implementations need no new virtuals.
// Request IDs must be echoed unchanged, including on failure. One page and one
// detail request may be active independently. Cancellation must suppress signals.
class SearchProviderContinuation
{
public:
    virtual ~SearchProviderContinuation() = default;
    virtual void requestPage(const QString &query, const QString &token, quint64 requestId) = 0;
    virtual bool supportsDetails() const
    {
        return false;
    }
    virtual void requestDetails(const QString &resultId, const QString &token, quint64 requestId)
    {
        Q_UNUSED(resultId);
        Q_UNUSED(token);
        Q_UNUSED(requestId);
    }
    virtual void cancelRequests() = 0;
};

#define SearchProviderContinuation_iid "org.kde.klookup.SearchProviderContinuation/1.0"
Q_DECLARE_INTERFACE(SearchProviderContinuation, SearchProviderContinuation_iid)

#endif
