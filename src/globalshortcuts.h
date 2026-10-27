#ifndef GLOBALSHORTCUTS_H
#define GLOBALSHORTCUTS_H

#include <QAction>
#include <QObject>
#include <QPointer>

class GlobalShortcuts : public QObject
{
    Q_OBJECT
public:
    enum class Status { Unavailable, Registered, Failed };
    explicit GlobalShortcuts(const QList<QAction *> &actions, QObject *parent = nullptr);
    Status status() const
    {
        return m_status;
    }
public slots:
    void refresh();

protected:
    virtual bool serviceAvailable() const;
    virtual bool registerAction(QAction *action);

private:
    QList<QPointer<QAction>> m_actions;
    Status m_status = Status::Unavailable;
};
#endif
