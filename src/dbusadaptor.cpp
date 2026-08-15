#include "dbusadaptor.h"
#include "mainwindow.h"

DBusAdaptor::DBusAdaptor(MainWindow *parent)
    : QDBusAbstractAdaptor(parent)
    , m_mainWindow(parent)
{
}

void DBusAdaptor::performSearch(const QString &query)
{
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
    m_mainWindow->performSearch(query);
}
