#include "searchtab.h"
#include "providerauthsession.h"
#include "searchcontinuation.h"
#include "sourceurl.h"
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <utility>

namespace
{
constexpr int ProviderRole = Qt::UserRole + 1;
constexpr int DetailLoadedRole = Qt::UserRole + 2;
constexpr int DetailTokenRole = Qt::UserRole + 3;
constexpr int MetadataRole = Qt::UserRole + 4;
constexpr int ContentRole = Qt::UserRole + 5;
} // namespace

SearchTab::SearchTab(const QString &query, QWidget *parent)
    : QWidget(parent), m_query(query), m_resultsTree(new QTreeWidget(this)), m_detailsView(new QTextBrowser(this)),
      m_metadataView(new QTreeWidget(this)), m_detailsButton(new QPushButton(QStringLiteral("Load full details"), this))
{
    auto *layout = new QHBoxLayout(this);
    auto *left = new QVBoxLayout;
    auto *label = new QLabel(QStringLiteral("Results for: ") + query, this);
    label->setTextFormat(Qt::PlainText);
    left->addWidget(label);
    m_resultsTree->setHeaderHidden(true);
    left->addWidget(m_resultsTree);
    auto *right = new QVBoxLayout;
    right->addWidget(m_detailsView);
    right->addWidget(m_metadataView, 1);
    m_metadataView->setObjectName(QStringLiteral("structuredMetadata"));
    m_metadataView->setHeaderLabels({QStringLiteral("Field / category"), QStringLiteral("Value")});
    m_metadataView->setWordWrap(true);
    m_metadataView->setColumnWidth(0, 200);
    m_metadataView->hide();
    connect(m_metadataView, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *item, int) {
        const auto url = Metadata::safeWebUrl(item->data(1, Qt::UserRole).toString());
        if (!url.isEmpty())
            QDesktopServices::openUrl(url);
    });
    right->addWidget(m_detailsButton);
    layout->addLayout(left, 1);
    layout->addLayout(right, 2);
    m_detailsButton->setObjectName(QStringLiteral("loadDetails"));
    m_detailsButton->hide();
    connect(m_detailsButton, &QPushButton::clicked, this, [this] { loadDetails(); });
    connect(m_resultsTree, &QTreeWidget::itemClicked, this, &SearchTab::onResultClicked);
    connect(m_resultsTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) { onResultClicked(item, 0); });
    m_detailsView->setOpenExternalLinks(false);
    m_detailsView->setOpenLinks(false);
    connect(m_detailsView, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        const auto safe = Metadata::safeWebUrl(url.toString(QUrl::FullyEncoded));
        if (!safe.isEmpty())
            QDesktopServices::openUrl(safe);
    });
}

SearchTab::~SearchTab()
{
    for (const auto &state : std::as_const(m_states)) {
        if (state.provider) {
            state.provider->disconnect(this);
            if (auto *continuation = qobject_cast<SearchProviderContinuation *>(state.provider)) {
                continuation->cancelRequests();
            }
            // Delete while the tab's state still exists; legacy providers own
            // their network managers/replies and Qt disconnects their callbacks.
            delete state.provider;
        }
    }
}

void SearchTab::startProvider(SearchProvider *provider, ProviderAuthSession *authentication)
{
    if (!provider || m_states.contains(provider->id())) {
        return;
    }
    provider->setParent(this);
    ProviderState state;
    state.provider = provider;
    state.authentication = authentication;
    if (authentication) {
        connect(provider, &SearchProvider::authenticationRejected, authentication, &ProviderAuthSession::invalidateAccessToken);
        connect(authentication, &ProviderAuthSession::credentialsChanged, provider,
                [provider](const QString &token) { provider->setCredentials({}, token); });
        if (authentication->hasUsableToken())
            provider->setCredentials({}, authentication->accessToken());
    }
    state.group = new QTreeWidgetItem(m_resultsTree);
    state.group->setText(0, provider->name());
    state.group->setFlags(state.group->flags() & ~Qt::ItemIsSelectable);
    state.group->setExpanded(true);
    const QString id = provider->id();
    m_states.insert(id, state);
    connect(provider, &SearchProvider::pageCompleted, this, [this, id](quint64 requestId, const SearchPage &page) {
        auto &state = m_states[id];
        if (!state.pending || state.pending != requestId)
            return;
        state.pending = 0;
        state.started = true;
        state.retryAt = {};
        // A repeated token cannot advance and must not create an endless loop.
        state.nextToken = state.seenTokens.contains(page.nextToken) ? QString() : page.nextToken;
        if (!state.nextToken.isEmpty())
            state.seenTokens.insert(state.nextToken);
        appendResults(state.group, page.results, &state);
        for (const auto &result : page.results) {
            if (auto *item = state.items.value(result.id);
                item && !item->data(0, DetailLoadedRole).toBool() && page.content.contains(result.id))
                item->setData(0, ContentRole, QVariant::fromValue(page.content.value(result.id)));
        }
        pageStatus(state,
                   state.nextToken.isEmpty() ? (state.items.isEmpty() ? QStringLiteral("No results") : QStringLiteral("No more results"))
                                             : QStringLiteral("Load more results"),
                   !state.nextToken.isEmpty());
    });
    connect(provider, &SearchProvider::searchCompleted, this, [this, id](const QString &, const QList<SearchResult> &results) {
        auto &state = m_states[id];
        if (qobject_cast<SearchProviderContinuation *>(state.provider) || !state.pending)
            return;
        state.pending = 0;
        state.started = true;
        if (state.status) {
            delete state.status;
            state.status = nullptr;
        }
        appendResults(state.group, results, &state);
        if (results.isEmpty())
            pageStatus(state, QStringLiteral("No results"), false);
    });
    connect(provider, &SearchProvider::searchFailed, this, [this, id](const QString &, const QString &error) {
        auto &state = m_states[id];
        if (qobject_cast<SearchProviderContinuation *>(state.provider) || !state.pending)
            return;
        state.pending = 0;
        pageStatus(state, QStringLiteral("Error: %1 — Retry").arg(error), true);
    });
    connect(provider, &SearchProvider::requestFailed, this, [this, id](quint64 requestId, const QString &error, int retryMs) {
        auto &state = m_states[id];
        if (requestId == state.pending && state.pending) {
            state.pending = 0;
            state.retryAt = QDateTime::currentDateTimeUtc().addMSecs(qBound(0, retryMs, 86400000));
            pageStatus(state, QStringLiteral("Error: %1 — Retry").arg(error), true);
        } else if (requestId == m_detailRequest && m_detailRequest && id == m_detailProvider) {
            m_detailRequest = 0;
            state.detailRetryAt = QDateTime::currentDateTimeUtc().addMSecs(qBound(0, retryMs, 86400000));
            if (m_selected && m_selected->data(0, ProviderRole).toString() == id &&
                m_selected->data(0, Qt::UserRole).value<SearchResult>().id == m_detailId) {
                renderResult(m_selected);
                m_detailsView->append(QStringLiteral("<p>%1</p>").arg((QStringLiteral("Details error: ") + error).toHtmlEscaped()));
            }
            updateDetailsControl();
            if (m_selected && (m_selected->data(0, ProviderRole).toString() != id ||
                               m_selected->data(0, Qt::UserRole).value<SearchResult>().id != m_detailId))
                loadInitialDetails();
        }
    });
    connect(provider, &SearchProvider::detailsCompleted, this, [this, id](quint64 requestId, const SearchDetails &details) {
        if (!m_detailRequest || m_detailRequest != requestId || id != m_detailProvider)
            return;
        m_detailRequest = 0;
        auto &state = m_states[id];
        auto *item = state.items.value(m_detailId);
        if (item && details.result.id == m_detailId) {
            item->setData(0, Qt::UserRole, QVariant::fromValue(details.result));
            item->setData(0, MetadataRole, QVariant::fromValue(details.metadata));
            item->setData(0, ContentRole, QVariant::fromValue(details.content));
            item->setData(0, DetailLoadedRole, true);
            const QString previousToken = item->data(0, DetailTokenRole).toString();
            item->setData(0, DetailTokenRole, details.nextToken == previousToken ? QString() : details.nextToken);
            item->setText(0, details.result.title);
            if (item == m_selected)
                renderResult(item);
        }
        updateDetailsControl();
        if (item != m_selected)
            loadInitialDetails();
    });
    loadMore(id);
}

void SearchTab::loadMore(const QString &providerId)
{
    auto it = m_states.find(providerId);
    if (it == m_states.end())
        return;
    auto &state = it.value();
    if (!state.provider || state.pending || (state.started && state.nextToken.isEmpty()) || state.retryAt > QDateTime::currentDateTimeUtc())
        return;
    state.pending = ++m_nextRequest;
    pageStatus(state, QStringLiteral("Loading…"), false);
    const quint64 requestId = state.pending;
    auto start = [this, providerId, requestId](bool ready, const QString &error) {
        auto &current = m_states[providerId];
        if (!current.provider || current.pending != requestId)
            return;
        if (!ready) {
            current.pending = 0;
            pageStatus(current, QStringLiteral("Authentication: %1 — Retry").arg(error), true);
            return;
        }
        if (auto *continuation = qobject_cast<SearchProviderContinuation *>(current.provider))
            continuation->requestPage(m_query, current.nextToken, requestId);
        else
            current.provider->search(m_query);
    };
    if (state.authentication)
        state.authentication->ensureReady(this, start);
    else
        start(true, {});
}

void SearchTab::pageStatus(ProviderState &state, const QString &text, bool actionable)
{
    if (state.status) {
        delete state.status;
        state.status = nullptr;
    }
    state.status = new QTreeWidgetItem(state.group);
    state.status->setFlags(state.status->flags() & ~Qt::ItemIsSelectable);
    state.status->setText(0, text);
    if (actionable) {
        auto *button = new QPushButton(text, m_resultsTree);
        button->setObjectName(QStringLiteral("loadMore_") + state.provider->id());
        const QString id = state.provider->id();
        connect(button, &QPushButton::clicked, this, [this, id] { loadMore(id); });
        const qint64 delay = QDateTime::currentDateTimeUtc().msecsTo(state.retryAt);
        if (delay > 0) {
            button->setEnabled(false);
            QTimer::singleShot(int(delay), button, [button] { button->setEnabled(true); });
        }
        m_resultsTree->setItemWidget(state.status, 0, button);
    }
}

QTreeWidgetItem *SearchTab::groupForName(const QString &name)
{
    const auto items = m_resultsTree->findItems(name, Qt::MatchExactly, 0);
    if (!items.isEmpty())
        return items.first();
    auto *group = new QTreeWidgetItem(m_resultsTree);
    group->setText(0, name);
    group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
    group->setExpanded(true);
    return group;
}

void SearchTab::appendResults(QTreeWidgetItem *group, const QList<SearchResult> &results, ProviderState *state)
{
    if (results.isEmpty() && (!state || state->items.isEmpty())) {
        // Attached providers use their status row for empty state.
        if (state)
            return;
        auto *item = new QTreeWidgetItem(group);
        item->setText(0, QStringLiteral("No results"));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }
    for (const auto &result : results) {
        QString identity = result.id;
        if (state && identity.isEmpty()) {
            if (qobject_cast<SearchProviderContinuation *>(state->provider))
                continue;
            // Legacy plugins are permitted to omit IDs; preserve their rows.
            identity = QStringLiteral("legacy:%1").arg(state->items.size());
        }
        if (state && state->items.contains(identity))
            continue;
        auto *item = new QTreeWidgetItem;
        item->setText(0, result.title);
        item->setData(0, Qt::UserRole, QVariant::fromValue(result));
        if (state) {
            item->setData(0, ProviderRole, state->provider->id());
            state->items.insert(identity, item);
        }
        group->insertChild(state && state->status ? group->indexOfChild(state->status) : group->childCount(), item);
    }
}

void SearchTab::addResults(const QString &providerName, const QList<SearchResult> &results, const QString &providerId)
{
    auto *group = groupForName(providerName);
    const int first = group->childCount();
    appendResults(group, results);
    for (int index = first; index < group->childCount(); ++index) {
        auto *item = group->child(index);
        if (item->data(0, Qt::UserRole).canConvert<SearchResult>())
            item->setData(0, ProviderRole, providerId.isEmpty() ? providerName : providerId);
    }
}

void SearchTab::setError(const QString &providerName, const QString &errorMsg)
{
    auto *item = new QTreeWidgetItem(groupForName(providerName));
    item->setText(0, QStringLiteral("Error: ") + errorMsg);
    item->setForeground(0, Qt::red);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
}

void SearchTab::onResultClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    m_selected = item && item->data(0, Qt::UserRole).isValid() ? item : nullptr;
    renderResult(m_selected);
    updateDetailsControl();
    loadInitialDetails();
    if (m_selected) emit resultViewed(m_selected->data(0, ProviderRole).toString(), m_selected->data(0, Qt::UserRole).value<SearchResult>());
}

void SearchTab::loadInitialDetails()
{
    if (m_selected && !m_selected->data(0, DetailLoadedRole).toBool())
        loadDetails();
}

void SearchTab::loadDetails(bool refresh)
{
    if (!m_selected || m_detailRequest)
        return;
    if (!refresh && m_selected->data(0, DetailLoadedRole).toBool() && m_selected->data(0, DetailTokenRole).toString().isEmpty())
        return;
    const auto result = m_selected->data(0, Qt::UserRole).value<SearchResult>();
    const QString id = m_selected->data(0, ProviderRole).toString();
    auto it = m_states.find(id);
    if (it == m_states.end() || !it->provider || it->detailRetryAt > QDateTime::currentDateTimeUtc())
        return;
    auto *continuation = qobject_cast<SearchProviderContinuation *>(it->provider);
    if (!continuation || !continuation->supportsDetails())
        return;
    m_detailProvider = id;
    m_detailId = result.id;
    m_detailRequest = ++m_nextRequest;
    const QString token = refresh ? QString() : m_selected->data(0, DetailTokenRole).toString();
    updateDetailsControl();
    const quint64 requestId = m_detailRequest;
    auto start = [this, id, resultId = result.id, token, requestId](bool ready, const QString &error) {
        if (m_detailRequest != requestId || !m_states.value(id).provider)
            return;
        auto *provider = m_states.value(id).provider.data();
        if (!ready) {
            emit provider->requestFailed(requestId, error);
            return;
        }
        if (auto *capability = qobject_cast<SearchProviderContinuation *>(provider))
            capability->requestDetails(resultId, token, requestId);
    };
    if (it->authentication)
        it->authentication->ensureReady(this, start);
    else
        start(true, {});
}

void SearchTab::updateDetailsControl()
{
    bool supported = false;
    QDateTime retryAt;
    if (m_selected && m_selected->data(0, Qt::UserRole).isValid()) {
        auto it = m_states.find(m_selected->data(0, ProviderRole).toString());
        if (it != m_states.end() && it->provider) {
            auto *continuation = qobject_cast<SearchProviderContinuation *>(it->provider);
            retryAt = it->detailRetryAt;
            supported = continuation && continuation->supportsDetails() &&
                        (!m_selected->data(0, DetailLoadedRole).toBool() || !m_selected->data(0, DetailTokenRole).toString().isEmpty());
        }
    }
    m_detailsButton->setVisible(supported);
    m_detailsButton->setEnabled(!m_detailRequest && retryAt <= QDateTime::currentDateTimeUtc());
    m_detailsButton->setText(m_detailRequest ? QStringLiteral("Loading details…")
                                             : (m_selected && !m_selected->data(0, DetailTokenRole).toString().isEmpty()
                                                    ? QStringLiteral("Load more content")
                                                    : QStringLiteral("Load full details")));
    const qint64 delay = QDateTime::currentDateTimeUtc().msecsTo(retryAt);
    if (delay > 0)
        QTimer::singleShot(int(delay), this, &SearchTab::updateDetailsControl);
}

void SearchTab::renderResult(QTreeWidgetItem *item)
{
    if (!item || !item->data(0, Qt::UserRole).isValid()) {
        m_detailsView->clear();
        m_metadataView->clear();
        m_metadataView->hide();
        return;
    }
    const auto res = item->data(0, Qt::UserRole).value<SearchResult>();
    QString html = QStringLiteral("<h2>%1</h2>").arg(res.title.toHtmlEscaped());
    if (!res.type.isEmpty()) {
        html += QStringLiteral("<p><b>Type:</b> %1</p>").arg(res.type.toHtmlEscaped());
    }
    const auto content = item->data(0, ContentRole).value<ResultContent>();
    if (!content.description.isEmpty())
        html += Content::render(content).html;
    else if (!res.description.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(res.description.toHtmlEscaped());

    if (!res.url.isEmpty()) {
        const QUrl qurl = validatedSourceUrl(res.url);
        if (!qurl.isEmpty()) {
            html += QStringLiteral("<p><a href=\"%1\">Open in Browser</a></p>").arg(QString::fromUtf8(qurl.toEncoded()).toHtmlEscaped());
        } else {
            html += QStringLiteral("<p>Invalid or unsupported URL: %1</p>").arg(res.url.toHtmlEscaped());
        }
    }

    if (!res.additionalData.isEmpty()) {
        html += QStringLiteral("<h3>Additional Info</h3><ul>");
        for (auto it = res.additionalData.cbegin(), end = res.additionalData.cend(); it != end; ++it) {
            const auto node = Metadata::fromVariant(it.key(), it.value());
            std::function<QString(const MetadataNode &)> renderNode = [&renderNode](const MetadataNode &entry) {
                QStringList values;
                for (const auto &value : entry.values)
                    values.append(value.text.toHtmlEscaped());
                QString text = QStringLiteral("<li><b>%1:</b> %2").arg(entry.label.toHtmlEscaped(), values.join(QStringLiteral(", ")));
                if (!entry.children.isEmpty()) {
                    text += QStringLiteral("<ul>");
                    for (const auto &child : entry.children)
                        text += renderNode(child);
                    text += QStringLiteral("</ul>");
                }
                return text + QStringLiteral("</li>");
            };
            html += renderNode(node);
        }
        html += QStringLiteral("</ul>");
    }
    m_detailsView->setHtml(html);
    renderMetadata(item->data(0, MetadataRole).value<ResultMetadata>());
}

void SearchTab::renderMetadata(const ResultMetadata &metadata)
{
    m_metadataView->clear();
    int remaining = 4000;
    std::function<void(QTreeWidgetItem *, const MetadataNode &, int)> append = [&](QTreeWidgetItem *parent, const MetadataNode &node,
                                                                                   int depth) {
        if (remaining-- <= 0 || depth > 16)
            return;
        if (node.values.isEmpty() && node.children.isEmpty())
            return;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_metadataView);
        item->setText(0, node.label);
        QStringList plainValues;
        int valueCount = 0;
        for (const auto &value : node.values) {
            if (++valueCount > 200 || remaining-- <= 0)
                break;
            const auto url = Metadata::safeWebUrl(value.url.toString(QUrl::FullyEncoded));
            if (value.kind == MetadataValue::Kind::Text || url.isEmpty())
                plainValues.append(value.text);
            else {
                auto *link = new QTreeWidgetItem(item);
                link->setText(0,
                              value.kind == MetadataValue::Kind::Image ? QStringLiteral("Image reference") : QStringLiteral("Source link"));
                link->setText(1, value.text.isEmpty() ? url.toDisplayString() : value.text);
                link->setToolTip(1, url.toDisplayString());
                link->setData(1, Qt::UserRole, url.toString(QUrl::FullyEncoded));
                link->setForeground(1, palette().link());
                link->setIcon(0, QIcon::fromTheme(QStringLiteral("internet-web-browser")));
            }
        }
        item->setText(1, plainValues.join(QStringLiteral("; ")));
        item->setToolTip(1, QStringLiteral("<p>%1</p>").arg(plainValues.join(QLatin1Char('\n')).toHtmlEscaped()));
        for (const auto &child : node.children)
            append(item, child, depth + 1);
        item->setExpanded(depth < 2);
    };
    for (const auto &section : metadata.sections)
        append(nullptr, section, 0);
    if (remaining <= 0) {
        auto *notice = new QTreeWidgetItem(m_metadataView);
        notice->setText(0, QStringLiteral("Additional metadata omitted at display limit"));
    }
    // Attribution stays visible even for an unusually large upstream record.
    remaining = qMax(remaining, 201);
    if (!metadata.attribution.isEmpty())
        append(nullptr, Metadata::text(QStringLiteral("Attribution"), metadata.attribution), 0);
    m_metadataView->setVisible(m_metadataView->topLevelItemCount() > 0);
}
