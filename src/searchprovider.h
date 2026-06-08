#ifndef SEARCHPROVIDER_H
#define SEARCHPROVIDER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct SearchResult {
    QString id;
    QString title;
    QString description;
    QString url;
    QString type;
    QVariantMap additionalData;
};
Q_DECLARE_METATYPE(SearchResult)

class SearchProvider : public QObject
{
    Q_OBJECT
public:
    explicit SearchProvider(QObject *parent = nullptr);
    virtual ~SearchProvider();

    virtual QString name() const = 0;
    virtual QString id() const = 0;

    virtual void search(const QString &query) = 0;

    bool isEnabled() const;
    void setEnabled(bool enabled);

    virtual SearchProvider* clone(QObject *parent = nullptr) const = 0;

signals:
    void searchCompleted(const QString &providerId, const QList<SearchResult> &results);
    void searchFailed(const QString &providerId, const QString &errorString);

private:
    bool m_enabled = true;
};

#endif // SEARCHPROVIDER_H
