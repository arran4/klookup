#include "../src/appicon.h"
#include <QImage>
#include <QtTest>

class AppIconTest : public QObject
{
    Q_OBJECT
private slots:
    void embeddedFallbackRendersWithoutInstalledTheme()
    {
        QIcon::setThemeSearchPaths({});
        QIcon::setThemeName(QStringLiteral("nonexistent-klookup-test-theme"));
        const QIcon icon = applicationIcon();
        QVERIFY(!icon.isNull());
        QVERIFY(QFile::exists(QStringLiteral(":/icons/org.kde.klookup.svg")));
        QVERIFY(QFile::exists(QStringLiteral(":/icons/org.kde.klookup.png")));
        for (int size : {16, 22, 32, 64, 128, 256}) {
            const auto pixmap = icon.pixmap(QSize(size, size));
            QVERIFY(!pixmap.isNull());
            QCOMPARE(pixmap.size(), QSize(size, size));
            const auto image = pixmap.toImage();
            QVERIFY(image.pixelColor(size / 2, size / 2).alpha() > 0);
        }
        const auto highDpi = icon.pixmap(QSize(32, 32), 2.0);
        QVERIFY(!highDpi.isNull());
        QCOMPARE(highDpi.size(), QSize(64, 64));
        QCOMPARE(highDpi.devicePixelRatio(), 2.0);
    }
    void fixedSizeAssets()
    {
        for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
            QImage image(QStringLiteral(ICON_SOURCE_DIR "/%1x%1/apps/org.kde.klookup.png").arg(size));
            QVERIFY(!image.isNull());
            QCOMPARE(image.size(), QSize(size, size));
            QVERIFY(image.hasAlphaChannel());
        }
    }
};
QTEST_MAIN(AppIconTest)
#include "test_appicon.moc"
