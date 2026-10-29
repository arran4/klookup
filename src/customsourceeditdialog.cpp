// SPDX-FileCopyrightText: 2026 KLookup contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "customsourceeditdialog.h"
#include "customhttpprovider.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

CustomSourceEditDialog::CustomSourceEditDialog(const CustomHttpSourceDefinition &initialDef, QWidget *parent)
    : QDialog(parent)
    , m_def(initialDef)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(initialDef.id.isEmpty() ? QStringLiteral("Add Custom Lookup Source")
                                          : QStringLiteral("Edit Custom Lookup Source - %1").arg(initialDef.name));
    resize(700, 600);
    setupUi();
    loadDefinition(initialDef);
}

CustomSourceEditDialog::~CustomSourceEditDialog()
{
    if (m_testProvider) {
        m_testProvider->cancelRequests();
    }
}

void CustomSourceEditDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    auto *tabWidget = new QTabWidget(this);

    // Tab 1: Request
    auto *requestTab = new QWidget(tabWidget);
    auto *reqLayout = new QFormLayout(requestTab);

    m_nameEdit = new QLineEdit(requestTab);
    m_nameEdit->setPlaceholderText(QStringLiteral("e.g. Hacker News Search"));
    reqLayout->addRow(QStringLiteral("Source Name:"), m_nameEdit);

    m_idEdit = new QLineEdit(requestTab);
    m_idEdit->setPlaceholderText(QStringLiteral("e.g. custom-hackernews"));
    reqLayout->addRow(QStringLiteral("Provider ID:"), m_idEdit);

    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_idEdit->text().isEmpty() || m_idEdit->text().startsWith(QStringLiteral("custom-"))) {
            QString slug = text.trimmed().toLower();
            slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
            m_idEdit->setText(QStringLiteral("custom-%1").arg(slug));
        }
    });

    m_descEdit = new QLineEdit(requestTab);
    m_descEdit->setPlaceholderText(QStringLiteral("Optional brief description"));
    reqLayout->addRow(QStringLiteral("Description:"), m_descEdit);

    m_methodCombo = new QComboBox(requestTab);
    m_methodCombo->addItem(QStringLiteral("GET"));
    m_methodCombo->addItem(QStringLiteral("POST"));
    reqLayout->addRow(QStringLiteral("HTTP Method:"), m_methodCombo);

    m_urlEdit = new QLineEdit(requestTab);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://hn.algolia.com/api/v1/search?query={query}"));
    reqLayout->addRow(QStringLiteral("URL Template:"), m_urlEdit);

    m_postTypeCombo = new QComboBox(requestTab);
    m_postTypeCombo->addItem(QStringLiteral("application/json"));
    m_postTypeCombo->addItem(QStringLiteral("application/x-www-form-urlencoded"));
    reqLayout->addRow(QStringLiteral("Content Type:"), m_postTypeCombo);

    m_bodyEdit = new QPlainTextEdit(requestTab);
    m_bodyEdit->setPlaceholderText(QStringLiteral("{\n  \"query\": \"{query}\",\n  \"limit\": 20\n}"));
    m_bodyEdit->setMaximumHeight(100);
    reqLayout->addRow(QStringLiteral("Body Template:"), m_bodyEdit);

    m_headersEdit = new QPlainTextEdit(requestTab);
    m_headersEdit->setPlaceholderText(QStringLiteral("Accept: application/json\nX-Custom-Header: value"));
    m_headersEdit->setMaximumHeight(80);
    reqLayout->addRow(QStringLiteral("Headers (Key: Value):"), m_headersEdit);

    m_authHeaderEdit = new QLineEdit(requestTab);
    m_authHeaderEdit->setPlaceholderText(QStringLiteral("e.g. Authorization"));
    reqLayout->addRow(QStringLiteral("Auth Header:"), m_authHeaderEdit);

    m_authSecretEdit = new QLineEdit(requestTab);
    m_authSecretEdit->setPlaceholderText(QStringLiteral("Bearer {secret} or API token"));
    reqLayout->addRow(QStringLiteral("Auth Template / Token:"), m_authSecretEdit);

    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CustomSourceEditDialog::onMethodChanged);
    onMethodChanged(0);

    tabWidget->addTab(requestTab, QStringLiteral("Request"));

    // Tab 2: Response Mapping
    auto *mappingTab = new QWidget(tabWidget);
    auto *mapLayout = new QFormLayout(mappingTab);

    m_resultsRootEdit = new QLineEdit(mappingTab);
    m_resultsRootEdit->setPlaceholderText(QStringLiteral("e.g. hits or results or . for top-level array"));
    mapLayout->addRow(QStringLiteral("Results Array Path:"), m_resultsRootEdit);

    m_titleFieldEdit = new QLineEdit(mappingTab);
    m_titleFieldEdit->setPlaceholderText(QStringLiteral("e.g. title or story_title"));
    mapLayout->addRow(QStringLiteral("Title Field:"), m_titleFieldEdit);

    m_urlFieldEdit = new QLineEdit(mappingTab);
    m_urlFieldEdit->setPlaceholderText(QStringLiteral("e.g. url or story_url"));
    mapLayout->addRow(QStringLiteral("URL Field:"), m_urlFieldEdit);

    m_descFieldEdit = new QLineEdit(mappingTab);
    m_descFieldEdit->setPlaceholderText(QStringLiteral("e.g. author or comment_text or summary"));
    mapLayout->addRow(QStringLiteral("Description Field:"), m_descFieldEdit);

    m_typeFieldEdit = new QLineEdit(mappingTab);
    m_typeFieldEdit->setPlaceholderText(QStringLiteral("e.g. _tags[0] or type"));
    mapLayout->addRow(QStringLiteral("Type / Category Field:"), m_typeFieldEdit);

    tabWidget->addTab(mappingTab, QStringLiteral("Field Mapping"));

    // Tab 3: Interactive Test & Preview
    auto *testTab = new QWidget(tabWidget);
    auto *testLayout = new QVBoxLayout(testTab);

    auto *testInputLayout = new QHBoxLayout;
    testInputLayout->addWidget(new QLabel(QStringLiteral("Test Query:"), testTab));
    m_testQueryEdit = new QLineEdit(QStringLiteral("kde plasma"), testTab);
    testInputLayout->addWidget(m_testQueryEdit);

    testInputLayout->addWidget(new QLabel(QStringLiteral("Secret Token:"), testTab));
    m_testSecretEdit = new QLineEdit(testTab);
    m_testSecretEdit->setEchoMode(QLineEdit::Password);
    m_testSecretEdit->setPlaceholderText(QStringLiteral("Optional secret for {secret}"));
    testInputLayout->addWidget(m_testSecretEdit);

    m_testBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), QStringLiteral("Send Test Request"), testTab);
    connect(m_testBtn, &QPushButton::clicked, this, &CustomSourceEditDialog::onTestClicked);
    testInputLayout->addWidget(m_testBtn);
    testLayout->addLayout(testInputLayout);

    m_testOutput = new QTextBrowser(testTab);
    m_testOutput->setPlaceholderText(QStringLiteral("Click 'Send Test Request' to view request diagnostics and parsed result preview."));
    testLayout->addWidget(m_testOutput);

    tabWidget->addTab(testTab, QStringLiteral("Test & Preview"));

    mainLayout->addWidget(tabWidget);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CustomSourceEditDialog::validateAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void CustomSourceEditDialog::onMethodChanged(int index)
{
    const bool isPost = (index == 1);
    m_postTypeCombo->setEnabled(isPost);
    m_bodyEdit->setEnabled(isPost);
}

void CustomSourceEditDialog::loadDefinition(const CustomHttpSourceDefinition &def)
{
    if (def.id.isEmpty()) return;

    m_nameEdit->setText(def.name);
    m_idEdit->setText(def.id);
    m_descEdit->setText(def.description);
    m_methodCombo->setCurrentIndex(def.method == CustomHttpSourceDefinition::Method::Post ? 1 : 0);
    m_urlEdit->setText(def.urlTemplate);

    m_postTypeCombo->setCurrentText(def.postContentType.isEmpty() ? QStringLiteral("application/json") : def.postContentType);
    m_bodyEdit->setPlainText(def.bodyTemplate);

    QStringList headerLines;
    for (auto it = def.headers.cbegin(); it != def.headers.cend(); ++it) {
        headerLines.append(QStringLiteral("%1: %2").arg(it.key(), it.value()));
    }
    m_headersEdit->setPlainText(headerLines.join(QLatin1Char('\n')));

    m_authHeaderEdit->setText(def.authHeader);
    m_authSecretEdit->setText(def.authSecretToken);

    m_resultsRootEdit->setText(def.mapping.resultsRoot);
    m_titleFieldEdit->setText(def.mapping.titleField);
    m_urlFieldEdit->setText(def.mapping.urlField);
    m_descFieldEdit->setText(def.mapping.descriptionField);
    m_typeFieldEdit->setText(def.mapping.typeField);
}

CustomHttpSourceDefinition CustomSourceEditDialog::definition() const
{
    CustomHttpSourceDefinition def = m_def;
    def.id = m_idEdit->text().trimmed();
    def.name = m_nameEdit->text().trimmed();
    def.description = m_descEdit->text().trimmed();
    def.method = m_methodCombo->currentIndex() == 1 ? CustomHttpSourceDefinition::Method::Post : CustomHttpSourceDefinition::Method::Get;
    def.urlTemplate = m_urlEdit->text().trimmed();
    def.postContentType = m_postTypeCombo->currentText();
    def.bodyTemplate = m_bodyEdit->toPlainText();

    def.headers.clear();
    const QStringList lines = m_headersEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon != -1) {
            def.headers.insert(line.left(colon).trimmed(), line.mid(colon + 1).trimmed());
        }
    }

    def.authHeader = m_authHeaderEdit->text().trimmed();
    def.authSecretToken = m_authSecretEdit->text().trimmed();

    def.mapping.resultsRoot = m_resultsRootEdit->text().trimmed();
    def.mapping.titleField = m_titleFieldEdit->text().trimmed();
    def.mapping.urlField = m_urlFieldEdit->text().trimmed();
    def.mapping.descriptionField = m_descFieldEdit->text().trimmed();
    def.mapping.typeField = m_typeFieldEdit->text().trimmed();

    def.trusted = true;
    def.hasSecret = def.requiresSecret();

    return def;
}

void CustomSourceEditDialog::validateAndAccept()
{
    CustomHttpSourceDefinition def = definition();
    QString error;
    if (!def.isValid(&error)) {
        QMessageBox::warning(this, QStringLiteral("Invalid Configuration"), error);
        return;
    }
    m_def = def;
    accept();
}

void CustomSourceEditDialog::onTestClicked()
{
    const QString query = m_testQueryEdit->text().trimmed();
    if (query.isEmpty()) {
        m_testOutput->setPlainText(QStringLiteral("Please enter a test query."));
        return;
    }

    CustomHttpSourceDefinition def = definition();
    QString validationError;
    if (!def.isValid(&validationError)) {
        m_testOutput->setHtml(QStringLiteral("<b style='color:red;'>Configuration Error:</b> %1").arg(validationError.toHtmlEscaped()));
        return;
    }

    m_testBtn->setEnabled(false);
    m_testOutput->setPlainText(QStringLiteral("Sending request..."));

    if (m_testProvider) {
        m_testProvider->cancelRequests();
        delete m_testProvider;
    }

    m_testProvider = new CustomHttpProvider(def, this, m_nam);
    const QString secret = !m_testSecretEdit->text().isEmpty() ? m_testSecretEdit->text() : def.authSecretToken;
    m_testProvider->setSecretToken(secret);

    connect(m_testProvider, &SearchProvider::searchCompleted, this, [this](const QString &, const QList<SearchResult> &results) {
        m_testBtn->setEnabled(true);
        QString html = QStringLiteral("<h3>Test Successful</h3>");
        html += QStringLiteral("<p><b>Results Received:</b> %1 item(s)</p><hr/>").arg(results.size());

        if (results.isEmpty()) {
            html += QStringLiteral("<p><i>No items extracted. Verify field mapping and results array root path.</i></p>");
        } else {
            html += QStringLiteral("<ol>");
            for (const auto &res : results) {
                html += QStringLiteral("<li><b>Title:</b> %1<br/><b>URL:</b> %2<br/><b>Description:</b> %3</li><br/>")
                            .arg(res.title.toHtmlEscaped(), res.url.toHtmlEscaped(), res.description.toHtmlEscaped());
            }
            html += QStringLiteral("</ol>");
        }
        m_testOutput->setHtml(html);
    });

    connect(m_testProvider, &SearchProvider::searchFailed, this, [this](const QString &, const QString &error) {
        m_testBtn->setEnabled(true);
        m_testOutput->setHtml(QStringLiteral("<b style='color:red;'>Request Failed:</b><br/>%1").arg(error.toHtmlEscaped()));
    });

    m_testProvider->search(query);
}
