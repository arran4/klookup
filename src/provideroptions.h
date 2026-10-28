#ifndef PROVIDEROPTIONS_H
#define PROVIDEROPTIONS_H

#include <QtPlugin>
class QWidget;

// Optional settings UI. Widgets hold drafts; saveOptions is called only on Apply
// or OK. Provider clones snapshot applied options for their entire tab lifetime.
class SearchProviderOptions
{
public:
    virtual ~SearchProviderOptions() = default;
    virtual QWidget *createOptionsWidget(QWidget *parent) const = 0;
    virtual void saveOptions(QWidget *widget) = 0;
};
#define SearchProviderOptions_iid "org.kde.klookup.SearchProviderOptions/1.0"
Q_DECLARE_INTERFACE(SearchProviderOptions, SearchProviderOptions_iid)
#endif
