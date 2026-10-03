#include "dbusadaptor.h"
#include "mainwindow.h"

DBusAdaptor::DBusAdaptor(MainWindow *parent)
    : QDBusAbstractAdaptor(parent)
    , m_mainWindow(parent)
{
}

void DBusAdaptor::performSearch(const QString &query)
{
    m_mainWindow->performSearch(query);
}
