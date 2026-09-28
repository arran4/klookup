#include "../src/searchprovider.h"

class ProviderMockAlpha : public SearchProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SearchProvider_iid)
    Q_INTERFACES(SearchProvider)
public:
    explicit ProviderMockAlpha(QObject *parent = nullptr) : SearchProvider(parent) {}
    QString name() const override { return QStringLiteral("Mock Alpha"); }
    QString id() const override { return QStringLiteral("mock_alpha"); }
    bool supportsAuthentication() const override { return true; }
    void search(const QString &query) override {
        QList<SearchResult> results = { {QStringLiteral("alpha1"), query, QStringLiteral("alpha desc"), QString(), QString(), {}} };
        emit searchCompleted(id(), results);
    }
    SearchProvider* clone(QObject *parent) const override {
        ProviderMockAlpha *p = new ProviderMockAlpha(parent);
        p->setEnabled(isEnabled());
        return p;
    }
};

#include "provider_mock_alpha.moc"
