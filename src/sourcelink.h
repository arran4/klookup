#ifndef SOURCELINK_H
#define SOURCELINK_H

#include "searchprovider.h"
#include <QToolButton>
class QMimeData;

class SourceLink : public QToolButton
{
    Q_OBJECT
public:
    explicit SourceLink(QWidget *parent = nullptr);
    void setResult(const SearchResult &result);
    QMimeData *mimeData() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    virtual void executeDrag(QMimeData *mime);

private:
    SearchResult m_result;
    QUrl m_url;
    QUrl m_pressedUrl;
    QPoint m_pressPosition;
    bool m_dragStarted = false;
};
#endif
