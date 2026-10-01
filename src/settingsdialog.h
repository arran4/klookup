#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>
#include <QListWidget>
#include <functional>

class SearchProvider;
class QCheckBox;

class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    enum class PersistResult { Success, Unavailable, OpenFailed, FolderFailed, WriteFailed };

    explicit SettingsDialog(const QList<SearchProvider*> &providers, QWidget *parent = nullptr);
    ~SettingsDialog();

    std::function<QString(QWidget*, const QString&, const QString&, bool*)> m_inputFunc;
    std::function<PersistResult(const QString&, const QString&, WId)> m_persistFunc;
    std::function<void(QWidget*, PersistResult, const QString&)> m_feedbackFunc;

private slots:
    void saveSettings();
    void showLoginDialog();

private:
    QList<SearchProvider*> m_providers;
    QListWidget *m_listWidget;

    QCheckBox *m_minimizeToTrayCheck;
    QCheckBox *m_monitorClipboardCheck;
    QCheckBox *m_autoSearchClipboardCheck;
    QCheckBox *m_onlyFocusedClipboardCheck;
};

#endif // SETTINGSDIALOG_H
