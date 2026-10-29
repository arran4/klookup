// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUSTOMSOURCEEDITDIALOG_H
#define CUSTOMSOURCEEDITDIALOG_H

#include "customhttpsource.h"
#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QNetworkAccessManager;
class CustomHttpProvider;

class CustomSourceEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CustomSourceEditDialog(const CustomHttpSourceDefinition &initialDef = CustomHttpSourceDefinition(),
                                    QWidget *parent = nullptr);
    ~CustomSourceEditDialog() override;

    CustomHttpSourceDefinition definition() const;

private slots:
    void onMethodChanged(int index);
    void onTestClicked();
    void validateAndAccept();

private:
    void setupUi();
    void loadDefinition(const CustomHttpSourceDefinition &def);

    QLineEdit *m_idEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_descEdit = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QComboBox *m_postTypeCombo = nullptr;
    QPlainTextEdit *m_bodyEdit = nullptr;
    QPlainTextEdit *m_headersEdit = nullptr;
    QLineEdit *m_authHeaderEdit = nullptr;
    QLineEdit *m_authSecretEdit = nullptr;

    QLineEdit *m_resultsRootEdit = nullptr;
    QLineEdit *m_titleFieldEdit = nullptr;
    QLineEdit *m_urlFieldEdit = nullptr;
    QLineEdit *m_descFieldEdit = nullptr;
    QLineEdit *m_typeFieldEdit = nullptr;

    QLineEdit *m_testQueryEdit = nullptr;
    QLineEdit *m_testSecretEdit = nullptr;
    QPushButton *m_testBtn = nullptr;
    QTextBrowser *m_testOutput = nullptr;

    CustomHttpSourceDefinition m_def;
    QNetworkAccessManager *m_nam = nullptr;
    CustomHttpProvider *m_testProvider = nullptr;
};

#endif // CUSTOMSOURCEEDITDIALOG_H
