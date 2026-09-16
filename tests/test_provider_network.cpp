#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "../providers/duckduckgo/provider_duckduckgo.h"

class TestProviderNetwork : public QObject
{
    Q_OBJECT
private slots:
    void test_replace_abort() {
        ProviderDuckDuckGo provider;

        QSignalSpy failSpy(&provider, &SearchProvider::searchFailed);
        QSignalSpy completeSpy(&provider, &SearchProvider::searchCompleted);

        // Start first request
        provider.search("first");

        // Start second request, replacing the first
        provider.search("second");

        // Process events to allow NetworkAccessManager to process the abort logic
        // We use wait on the spy so we don't indefinitely block.
        failSpy.wait(500);
        completeSpy.wait(500);

        // Should not have emitted a failure for the aborted request
        QCOMPARE(failSpy.count(), 0);
    }
};

QTEST_MAIN(TestProviderNetwork)
#include "test_provider_network.moc"
