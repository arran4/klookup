#include "../src/searchprovider.h"

class ProviderMockBeta : public SearchProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderMockBeta(QObject *parent = nullptr) : SearchProvider(parent) {}
    QString name() const override { return QStringLiteral("Mock Beta"); }
    QString id() const override { return QStringLiteral("mock_beta"); }
    void search(const QString &query) override {
        QList<SearchResult> results = { {QStringLiteral("beta1"), query, QStringLiteral("beta desc"), QString(), QString(), {}} };
        emit searchCompleted(id(), results);
    }
    SearchProvider* clone(QObject *parent) const override {
        ProviderMockBeta *p = new ProviderMockBeta(parent);
        p->setEnabled(isEnabled());
        return p;
    }
};

#include "provider_mock_beta.moc"
