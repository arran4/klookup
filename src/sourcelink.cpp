#include "sourcelink.h"
#include "resultactions.h"
#include "sourceurl.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDrag>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QShortcut>

SourceLink::SourceLink(QWidget *parent) : QToolButton(parent)
{
    setObjectName(QStringLiteral("sourceLink"));
    setText(QStringLiteral("Open in Browser"));
    setIcon(QIcon::fromTheme(QStringLiteral("internet-web-browser")));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setAutoRaise(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Result source link"));
    connect(this, &QToolButton::clicked, this, [this] {
        if (!m_url.isEmpty())
            QDesktopServices::openUrl(m_url);
    });
    auto *copy = new QShortcut(QKeySequence::Copy, this);
    copy->setContext(Qt::WidgetShortcut);
    connect(copy, &QShortcut::activated, this, [this] {
        if (auto *data = mimeData())
            QGuiApplication::clipboard()->setMimeData(data);
    });
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (m_url.isEmpty())
            return;
        auto *menu = new QMenu(this);
        auto *commands = new ResultActions(m_result, false, menu);
        menu->addAction(commands->copyUrl);
        menu->addAction(commands->openUrl);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->popup(mapToGlobal(position));
    });
    hide();
}

void SourceLink::setResult(const SearchResult &result)
{
    m_result = result;
    m_url = validatedSourceUrl(result.url);
    setVisible(!m_url.isEmpty());
    setEnabled(!m_url.isEmpty());
    setToolTip(m_url.isEmpty() ? QString()
                               : m_url.toDisplayString() + QStringLiteral("\nClick to open; drag to another application; Ctrl+C to copy"));
    setAccessibleDescription(m_url.toDisplayString());
}

QMimeData *SourceLink::mimeData() const
{
    return ResultActions::urlMimeData(m_url);
}

void SourceLink::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPosition = event->position().toPoint();
        m_pressedUrl = m_url;
        m_dragStarted = false;
    }
    QToolButton::mousePressEvent(event);
}

void SourceLink::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_pressedUrl.isEmpty() && !m_dragStarted &&
        (event->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragStarted = true;
        setDown(false); // a drag must never emit the button's click action
        event->accept();
        executeDrag(ResultActions::urlMimeData(m_pressedUrl));
        return;
    }
    QToolButton::mouseMoveEvent(event);
}

void SourceLink::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragStarted && event->button() == Qt::LeftButton) {
        m_dragStarted = false;
        m_pressedUrl = QUrl();
        setDown(false);
        event->accept();
        return;
    }
    m_pressedUrl = QUrl();
    QToolButton::mouseReleaseEvent(event);
}

void SourceLink::executeDrag(QMimeData *mime)
{
    if (!mime)
        return;
    QPointer<QDrag> drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(icon().pixmap(24, 24));
    drag->exec(Qt::CopyAction | Qt::LinkAction, Qt::LinkAction);
    if (drag)
        drag->deleteLater();
}
