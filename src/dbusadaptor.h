#ifndef DBUSADAPTOR_H
#define DBUSADAPTOR_H

#include <QDBusAbstractAdaptor>
#include <QString>

class MainWindow;

class DBusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.klookup")

public:
    explicit DBusAdaptor(MainWindow *parent);

public slots:
    void performSearch(const QString &query);
    void openSearchInNewWindow(const QString &query);

private:
    MainWindow *m_mainWindow;
};

#endif // DBUSADAPTOR_H
