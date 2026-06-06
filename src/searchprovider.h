#ifndef SEARCHPROVIDER_H
#define SEARCHPROVIDER_H

#include <QObject>
#include <QString>
#include <QVariantList>

struct SearchResult {
    QString id;
    QString title;
    QString description;
    QString url;
    QString type;
    QVariantMap additionalData;
};

class SearchProvider : public QObject
{
    Q_OBJECT
public:
    explicit SearchProvider(QObject *parent = nullptr);
    virtual ~SearchProvider();

    virtual QString name() const = 0;
    virtual QString id() const = 0;

    // Instead of emitting signals, return a job or emit with a query context so the receiver can filter it,
    // or instantiate a fresh provider for each tab. Instantiating a fresh provider is cleaner.
    virtual void search(const QString &query) = 0;

    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Virtual clone function to allow creating discrete instances per tab
    virtual SearchProvider* clone(QObject *parent = nullptr) const = 0;

signals:
    void searchCompleted(const QString &providerId, const QList<SearchResult> &results);
    void searchFailed(const QString &providerId, const QString &errorString);

private:
    bool m_enabled = true;
};

#endif // SEARCHPROVIDER_H
