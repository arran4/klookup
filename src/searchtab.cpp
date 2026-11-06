#include "searchtab.h"
#include "splitterkeyboard.h"
#include "providerauthsession.h"
#include "resultactions.h"
#include "resultdetailwindow.h"
#include "searchcontinuation.h"
#include "sourcelink.h"
#include "sourceurl.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
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
    : QWidget(parent)
    , m_query(query)
    , m_resultsTree(nullptr)
    , m_detailsView(nullptr)
    , m_metadataView(nullptr)
    , m_sourceLink(nullptr)
    , m_detailsButton(nullptr)
{
    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    rootLayout->addWidget(m_mainSplitter);

    auto *leftWidget = new QWidget;
    leftWidget->setObjectName(QStringLiteral("resultsPane"));
    leftWidget->setMinimumWidth(150);
    auto *left = new QVBoxLayout(leftWidget);
    left->setContentsMargins(0, 0, 0, 0);

    auto *headerLayout = new QHBoxLayout;
    auto *label = new QLabel(QStringLiteral("Results for: ") + query, leftWidget);
    label->setTextFormat(Qt::PlainText);
    headerLayout->addWidget(label);
    headerLayout->addStretch();
    auto *detachButton = new QPushButton(QIcon::fromTheme(QStringLiteral("window-new")), QStringLiteral("Open in New Window"), leftWidget);
    detachButton->setObjectName(QStringLiteral("openSearchInNewWindow"));
    connect(detachButton, &QPushButton::clicked, this, [this] {
        emit openSearchInNewWindowRequested(m_query);
    });
    headerLayout->addWidget(detachButton);
    left->addLayout(headerLayout);

    m_resultsTree = new QTreeWidget(leftWidget);
    m_resultsTree->setObjectName(QStringLiteral("resultsTree"));
    m_resultsTree->setHeaderHidden(true);
    m_resultsTree->installEventFilter(this);
    m_resultsTree->viewport()->installEventFilter(this);
    for (const auto &key : {QKeySequence(Qt::SHIFT | Qt::Key_F10), QKeySequence(Qt::Key_Menu)}) {
        auto *shortcut = new QShortcut(key, m_resultsTree);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this] {
            auto *item = m_resultsTree->currentItem();
            if (item)
                showResultMenu(item, m_resultsTree->viewport()->mapToGlobal(m_resultsTree->visualItemRect(item).center()));
        });
    }
    left->addWidget(m_resultsTree);

    m_detailStack = new QStackedWidget;
    m_detailStack->setObjectName(QStringLiteral("detailStack"));
    m_detailStack->setMinimumWidth(200);

    m_emptyStateWidget = createEmptyStateWidget(m_detailStack);
    m_detailStack->addWidget(m_emptyStateWidget);

    m_detailContainer = new QWidget(m_detailStack);
    m_detailContainer->setObjectName(QStringLiteral("detailContainer"));
    auto *right = new QVBoxLayout(m_detailContainer);
    right->setContentsMargins(0, 0, 0, 0);

    m_detailSplitter = new QSplitter(Qt::Vertical, m_detailContainer);
    m_detailSplitter->setObjectName(QStringLiteral("detailSplitter"));

    m_detailsView = new QTextBrowser(m_detailSplitter);
    m_detailsView->setObjectName(QStringLiteral("detailsView"));
    m_detailsView->setMinimumHeight(60);

    m_metadataView = new QTreeWidget(m_detailSplitter);
    m_metadataView->setObjectName(QStringLiteral("structuredMetadata"));
    m_metadataView->setHeaderLabels({QStringLiteral("Field / category"), QStringLiteral("Value")});
    m_metadataView->setWordWrap(true);
    m_metadataView->setColumnWidth(0, 200);
    m_metadataView->setMinimumHeight(50);
    m_metadataView->hide();

    m_detailSplitter->insertWidget(0, m_metadataView);
    m_detailSplitter->insertWidget(0, m_detailsView);

    m_detailSplitter->setChildrenCollapsible(true);
    m_detailSplitter->setCollapsible(0, false);
    m_detailSplitter->setCollapsible(1, true);

    connect(m_metadataView, &QTreeWidget::itemActivated, this, [](QTreeWidgetItem *item, int) {
        const auto url = Metadata::safeWebUrl(item->data(1, Qt::UserRole).toString());
        if (!url.isEmpty())
            QDesktopServices::openUrl(url);
    });

    right->addWidget(m_detailSplitter, 1);

    m_sourceLink = new SourceLink(m_detailContainer);
    right->addWidget(m_sourceLink);

    m_detailsButton = new QPushButton(QStringLiteral("Load full details"), m_detailContainer);
    m_detailsButton->setObjectName(QStringLiteral("loadDetails"));
    m_detailsButton->hide();
    connect(m_detailsButton, &QPushButton::clicked, this, [this] { loadDetails(); });
    right->addWidget(m_detailsButton);

    m_openDetailsWindowButton = new QPushButton(QIcon::fromTheme(QStringLiteral("window-new")), QStringLiteral("Open Details in New Window"), m_detailContainer);
    m_openDetailsWindowButton->setObjectName(QStringLiteral("openDetailsInNewWindowButton"));
    m_openDetailsWindowButton->hide();
    connect(m_openDetailsWindowButton, &QPushButton::clicked, this, [this] {
        if (m_selected) {
            openResultDetailsInNewWindow(m_selected);
        }
    });
    right->addWidget(m_openDetailsWindowButton);

    m_detailStack->addWidget(m_detailContainer);

    m_mainSplitter->insertWidget(0, m_detailStack);
    m_mainSplitter->insertWidget(0, leftWidget);

    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setCollapsible(0, false);
    m_mainSplitter->setCollapsible(1, false);

    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 2);
    m_detailSplitter->setStretchFactor(0, 1);
    m_detailSplitter->setStretchFactor(1, 1);

    m_detailStack->setCurrentWidget(m_emptyStateWidget);

    connect(m_resultsTree, &QTreeWidget::itemClicked, this, &SearchTab::onResultClicked);
    connect(m_resultsTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) { onResultClicked(item, 0); });
    m_detailsView->setOpenExternalLinks(false);
    m_detailsView->setOpenLinks(false);
    m_detailsView->document()->setResourceProvider([](const QUrl &) { return QVariant(QByteArray()); });
    connect(m_detailsView, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        const auto safe = Metadata::safeWebUrl(url.toString(QUrl::FullyEncoded));
        if (!safe.isEmpty())
            QDesktopServices::openUrl(safe);
    });

    if (auto *handle = m_mainSplitter->handle(1)) {
        handle->setObjectName(QStringLiteral("mainSplitterHandle"));
        handle->setFocusPolicy(Qt::StrongFocus);
        handle->setAccessibleName(QStringLiteral("Search results and details splitter"));
        handle->setAccessibleDescription(QStringLiteral("Use left and right arrow keys to resize the results list and detail panes. Double-click or press Enter to reset."));
        handle->installEventFilter(this);
    }
    if (auto *handle = m_detailSplitter->handle(1)) {
        handle->setObjectName(QStringLiteral("detailSplitterHandle"));
        handle->setFocusPolicy(Qt::StrongFocus);
        handle->setAccessibleName(QStringLiteral("Detail summary and metadata splitter"));
        handle->setAccessibleDescription(QStringLiteral("Use up and down arrow keys to resize the summary and metadata panes. Double-click or press Enter to reset."));
        handle->installEventFilter(this);
    }

    connect(m_mainSplitter, &QSplitter::splitterMoved, this, [this] {
        saveLayout(m_mainSplitter);
    });
    connect(m_detailSplitter, &QSplitter::splitterMoved, this, [this] {
        m_savedDetailSizes = m_detailSplitter->sizes();
        saveLayout(m_detailSplitter);
    });

    restoreLayout();
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
    state.group->setData(0, Qt::UserRole + 20, QVariant::fromValue(provider->classification()));
    state.group->setToolTip(0, provider->classification().description().toHtmlEscaped());
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
        return;
    }

    QMap<QString, QTreeWidgetItem *> itemById;
    if (state) {
        itemById = state->items;
    } else {
        std::function<void(QTreeWidgetItem *)> collect = [&](QTreeWidgetItem *it) {
            if (it->data(0, Qt::UserRole).canConvert<SearchResult>()) {
                const auto res = it->data(0, Qt::UserRole).value<SearchResult>();
                if (!res.id.isEmpty()) {
                    itemById.insert(res.id, it);
                }
            }
            for (int i = 0; i < it->childCount(); ++i) {
                collect(it->child(i));
            }
        };
        for (int i = 0; i < group->childCount(); ++i) {
            collect(group->child(i));
        }
    }

    QMultiHash<QString, QTreeWidgetItem *> pendingChildren;

    std::function<void(QTreeWidgetItem *, const SearchResult &)> addResultItem =
        [&](QTreeWidgetItem *parentItem, const SearchResult &result) {
            QString identity = result.id;
            if (state && identity.isEmpty()) {
                if (qobject_cast<SearchProviderContinuation *>(state->provider))
                    return;
                // Legacy plugins are permitted to omit IDs; preserve their rows.
                identity = QStringLiteral("legacy:%1").arg(state->items.size());
            }
            if (!identity.isEmpty() && itemById.contains(identity))
                return;

            auto *item = new QTreeWidgetItem;
            item->setText(0, result.title);
            item->setData(0, Qt::UserRole, QVariant::fromValue(result));
            if (!identity.isEmpty()) {
                itemById.insert(identity, item);
                if (state) {
                    state->items.insert(identity, item);
                }
            }
            if (state) {
                item->setData(0, ProviderRole, state->provider->id());
            }

            if (parentItem) {
                parentItem->addChild(item);
                parentItem->setExpanded(true);
            } else if (!result.parentId.isEmpty()) {
                if (itemById.contains(result.parentId)) {
                    auto *p = itemById.value(result.parentId);
                    p->addChild(item);
                    p->setExpanded(true);
                } else {
                    pendingChildren.insert(result.parentId, item);
                }
            } else {
                group->insertChild(state && state->status ? group->indexOfChild(state->status) : group->childCount(), item);
            }

            if (!identity.isEmpty() && pendingChildren.contains(identity)) {
                const auto waiting = pendingChildren.values(identity);
                for (auto *childItem : waiting) {
                    item->addChild(childItem);
                    item->setExpanded(true);
                }
                pendingChildren.remove(identity);
            }

            for (const auto &child : result.children) {
                addResultItem(item, child);
            }
        };

    for (const auto &result : results) {
        addResultItem(nullptr, result);
    }

    for (auto *orphan : pendingChildren) {
        group->insertChild(state && state->status ? group->indexOfChild(state->status) : group->childCount(), orphan);
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
        m_sourceLink->setResult({});
        if (m_openDetailsWindowButton) {
            m_openDetailsWindowButton->hide();
        }
        if (m_detailStack) {
            m_detailStack->setCurrentWidget(m_emptyStateWidget);
        }
        return;
    }
    if (m_detailStack) {
        m_detailStack->setCurrentWidget(m_detailContainer);
    }
    if (m_openDetailsWindowButton) {
        m_openDetailsWindowButton->show();
    }
    const auto res = item->data(0, Qt::UserRole).value<SearchResult>();
    m_sourceLink->setResult(res);
    const auto content = item->data(0, ContentRole).value<ResultContent>();
    const QString providerId = item->data(0, ProviderRole).toString();
    const QString providerName = m_states.contains(providerId) && m_states[providerId].provider
                                     ? m_states[providerId].provider->name()
                                     : (item->parent() ? item->parent()->text(0) : providerId);

    m_detailsView->setHtml(Content::renderResultHtml(res, content, providerName));
    renderMetadata(item->data(0, MetadataRole).value<ResultMetadata>());
}

void SearchTab::renderMetadata(const ResultMetadata &metadata)
{
    const bool wasVisible = m_metadataView->isVisible();
    Metadata::populateMetadataTree(m_metadataView, metadata);
    if (!wasVisible && m_metadataView->isVisible() && m_detailSplitter) {
        if (m_savedDetailSizes.size() == 2 && m_savedDetailSizes[0] > 0) {
            m_detailSplitter->setSizes(m_savedDetailSizes);
        }
    }
}

QMenu *SearchTab::createResultMenu(QTreeWidgetItem *item)
{
    if (!item || !item->data(0, Qt::UserRole).canConvert<SearchResult>())
        return nullptr;
    m_resultsTree->setCurrentItem(item);
    onResultClicked(item, 0);
    const SearchResult result = item->data(0, Qt::UserRole).value<SearchResult>();
    const QString providerId = item->data(0, ProviderRole).toString();
    const auto state = m_states.value(providerId);
    auto *continuation = state.provider ? qobject_cast<SearchProviderContinuation *>(state.provider) : nullptr;
    const bool canRefresh = continuation && continuation->supportsDetails() && !m_detailRequest && m_detailsButton->isEnabled();
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("resultContextMenu"));
    auto *commands = new ResultActions(result, canRefresh, menu);
    menu->addAction(commands->refresh);
    menu->addAction(commands->openDetailsInNewWindow);
    menu->addSeparator();
    menu->addAction(commands->copyUrl);
    menu->addAction(commands->openUrl);
    menu->addAction(commands->copyTitle);
    connect(commands, &ResultActions::openDetailsInNewWindowRequested, this, [this, item] {
        openResultDetailsInNewWindow(item);
    });
    connect(commands, &ResultActions::refreshRequested, this, [this, providerId, resultId = result.id] {
        const auto state = m_states.value(providerId);
        auto *target = state.items.value(resultId);
        if (!target)
            return;
        m_resultsTree->setCurrentItem(target);
        onResultClicked(target, 0);
        loadDetails(true);
    });
    return menu;
}

ResultDetailWindow *SearchTab::openResultDetailsInNewWindow(QTreeWidgetItem *item)
{
    if (!item || !item->data(0, Qt::UserRole).canConvert<SearchResult>()) {
        return nullptr;
    }
    const SearchResult result = item->data(0, Qt::UserRole).value<SearchResult>();
    const QString providerName = item->parent() ? item->parent()->text(0) : QString();
    const auto metadata = item->data(0, MetadataRole).value<ResultMetadata>();
    const auto content = item->data(0, ContentRole).value<ResultContent>();

    auto *detailWin = new ResultDetailWindow(result, providerName, metadata, content);
    detailWin->show();
    detailWin->raise();
    detailWin->activateWindow();
    emit detailWindowCreated(detailWin);
    return detailWin;
}

void SearchTab::showResultMenu(QTreeWidgetItem *item, const QPoint &globalPosition)
{
    auto *menu = createResultMenu(item);
    if (!menu)
        return;
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    menu->popup(globalPosition);
}

bool SearchTab::eventFilter(QObject *watched, QEvent *event)
{
    if (m_mainSplitter && watched == m_mainSplitter->handle(1)) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            resetMainSplitterSizes();
            return true;
        } else if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (resizeSplitterWithKey(m_mainSplitter, keyEvent)) return true;
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Space) {
                resetMainSplitterSizes();
                return true;
            }
        }
    } else if (m_detailSplitter && watched == m_detailSplitter->handle(1)) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            resetDetailSplitterSizes();
            return true;
        } else if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (resizeSplitterWithKey(m_detailSplitter, keyEvent)) return true;
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Space) {
                resetDetailSplitterSizes();
                return true;
            }
        }
    }

    if ((watched == m_resultsTree || watched == m_resultsTree->viewport()) && event->type() == QEvent::ContextMenu) {
        auto *context = static_cast<QContextMenuEvent *>(event);
        auto *item = context->reason() == QContextMenuEvent::Keyboard
                         ? m_resultsTree->currentItem()
                         : m_resultsTree->itemAt(m_resultsTree->viewport()->mapFromGlobal(context->globalPos()));
        const QPoint position = context->reason() == QContextMenuEvent::Keyboard && item
                                    ? m_resultsTree->viewport()->mapToGlobal(m_resultsTree->visualItemRect(item).center())
                                    : context->globalPos();
        showResultMenu(item, position);
        event->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QSplitter *SearchTab::mainSplitter() const
{
    return m_mainSplitter;
}

QSplitter *SearchTab::detailSplitter() const
{
    return m_detailSplitter;
}

QWidget *SearchTab::emptyStateWidget() const
{
    return m_emptyStateWidget;
}

void SearchTab::saveLayout(QSplitter *changed)
{
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
    if (changed == m_mainSplitter) {
        const auto sizes = m_mainSplitter->sizes();
        if (sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 0) {
            group.writeEntry(QStringLiteral("SearchTabSplitter"), sizes);
        }
    }
    if (changed == m_detailSplitter && m_savedDetailSizes.size() == 2 && m_savedDetailSizes[0] > 0) {
        group.writeEntry(QStringLiteral("SearchTabDetailSplitter"), m_savedDetailSizes);
    }
    group.sync();
}

void SearchTab::restoreLayout()
{
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("klookuprc")), QStringLiteral("Layout"));
    const QList<int> defaultMainSizes = {260, 540};
    const QList<int> defaultDetailSizes = {250, 200};

    if (m_mainSplitter) {
        QList<int> mainSizes = group.readEntry(QStringLiteral("SearchTabSplitter"), defaultMainSizes);
        if (mainSizes.size() == 2 && mainSizes[0] > 0 && mainSizes[1] > 0) {
            m_mainSplitter->setSizes(mainSizes);
        } else {
            m_mainSplitter->setSizes(defaultMainSizes);
        }
    }

    QList<int> detailSizes = group.readEntry(QStringLiteral("SearchTabDetailSplitter"), defaultDetailSizes);
    if (detailSizes.size() == 2 && detailSizes[0] > 0 && detailSizes[1] >= 0) {
        m_savedDetailSizes = detailSizes;
    } else {
        m_savedDetailSizes = defaultDetailSizes;
    }

    if (m_detailSplitter) {
        m_detailSplitter->setSizes(m_savedDetailSizes);
    }
}

void SearchTab::resetMainSplitterSizes()
{
    if (!m_mainSplitter)
        return;
    const int total = m_mainSplitter->width();
    if (total > 0) {
        const int left = qMax(150, total / 3);
        const int right = qMax(200, total - left);
        m_mainSplitter->setSizes({left, right});
    } else {
        m_mainSplitter->setSizes({260, 540});
    }
    saveLayout(m_mainSplitter);
}

void SearchTab::resetDetailSplitterSizes()
{
    if (!m_detailSplitter)
        return;
    const int total = m_detailSplitter->height();
    if (total > 0) {
        const int top = qMax(60, total / 2);
        const int bottom = qMax(50, total - top);
        m_savedDetailSizes = {top, bottom};
    } else {
        m_savedDetailSizes = {250, 200};
    }
    m_detailSplitter->setSizes(m_savedDetailSizes);
    saveLayout(m_detailSplitter);
}

QWidget *SearchTab::createEmptyStateWidget(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("emptyDetailState"));
    widget->setAccessibleName(QStringLiteral("No result selected"));
    widget->setAccessibleDescription(QStringLiteral("Select a search result from the list to view its details, summary, and metadata."));

    auto *layout = new QVBoxLayout(widget);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(10);
    layout->setContentsMargins(20, 20, 20, 20);

    auto *iconLabel = new QLabel(widget);
    iconLabel->setObjectName(QStringLiteral("emptyStateIcon"));
    iconLabel->setAlignment(Qt::AlignCenter);
    QIcon icon = QIcon::fromTheme(QStringLiteral("edit-find"));
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("system-search"));
    }
    if (!icon.isNull()) {
        iconLabel->setPixmap(icon.pixmap(48, 48));
    }
    layout->addWidget(iconLabel);

    auto *titleLabel = new QLabel(QStringLiteral("No result selected"), widget);
    titleLabel->setObjectName(QStringLiteral("emptyStateTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    auto *descLabel = new QLabel(QStringLiteral("Select a search result on the left to view its details, summary, and metadata."), widget);
    descLabel->setObjectName(QStringLiteral("emptyStateDescription"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    descLabel->setMaximumWidth(360);
    QPalette pal = descLabel->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
    descLabel->setPalette(pal);
    layout->addWidget(descLabel);

    return widget;
}
