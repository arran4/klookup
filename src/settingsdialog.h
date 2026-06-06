#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QListWidget>

class SearchProvider;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent = nullptr);
    ~SettingsDialog();

private slots:
    void saveSettings();

private:
    QList<SearchProvider*> m_providers;
    QListWidget *m_listWidget;
};

#endif // SETTINGSDIALOG_H
