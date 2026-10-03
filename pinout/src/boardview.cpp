#include "boardview.h"

#include "chiprender.h"

#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace pinout {

namespace {
constexpr qreal kZoomFactor = 1.15;
constexpr qreal kZoomMin = 0.08;
constexpr qreal kZoomMax = 10.0;
constexpr qreal kSpacingMin = 4.0;
constexpr qreal kSpacingMax = 200.0;
constexpr int kMultiMax = 500;
constexpr QColor kAccent(0x1E, 0x88, 0xE5);  // 预览高亮
constexpr QColor kCrosshair(0x53, 0x6D, 0x7A, 170);
}  // namespace

BoardView::BoardView(BoardScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), m_boardScene(scene)
{
    setRenderHint(QPainter::Antialiasing, true);
    viewport()->setMouseTracking(true);  // 无按键也收 move，驱动十字光标
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void BoardView::setTool(Tool tool)
{
    m_tool = tool;
    m_anchorSet = false;
    const bool interactive = (tool == Tool::Select);  // 仅选择模式可拖动/框选
    m_boardScene->setPinsInteractive(interactive);
    setDragMode(interactive ? QGraphicsView::RubberBandDrag : QGraphicsView::NoDrag);
    // 统一在 viewport() 上设光标：viewport 一旦被显式 setCursor 就不再继承 view 的，
    // 两级混用会导致平移/空格后光标卡死
    viewport()->setCursor(tool == Tool::Select ? Qt::ArrowCursor
                                               : tool == Tool::AppendDef ? Qt::PointingHandCursor
                                                                         : Qt::CrossCursor);
    viewport()->update();
}

void BoardView::setSpacing(qreal spacing)
{
    m_spacing = std::clamp(spacing, kSpacingMin, kSpacingMax);
    viewport()->update();
}

void BoardView::setPinSize(qreal size)
{
    m_pinSize = size;
    viewport()->update();
}

void BoardView::fitBoard()
{
    const QRectF board = m_boardScene->boardRect();
    if (board.isEmpty())
        return;
    fitInView(board.adjusted(-60, -60, 60, 120), Qt::KeepAspectRatio);
    emit zoomChanged(transform().m11());
}

void BoardView::zoom100()
{
    resetTransform();
    emit zoomChanged(1.0);
}

void BoardView::zoomStepIn()
{
    zoomAt(m_hasCursor ? m_cursor : mapToScene(viewport()->rect().center()), kZoomFactor);
}

void BoardView::zoomStepOut()
{
    zoomAt(m_hasCursor ? m_cursor : mapToScene(viewport()->rect().center()), 1.0 / kZoomFactor);
}

void BoardView::zoomAt(const QPointF& scenePos, qreal factor)
{
    const qreal current = transform().m11();
    const qreal target = std::clamp(current * factor, kZoomMin, kZoomMax);
    if (qFuzzyCompare(current, target))
        return;
    const QPoint viewportPos = mapFromScene(scenePos);
    scale(target / current, target / current);
    // 滚动补偿，使 scenePos 保持在原视口像素位置（光标锚点缩放）
    const QPointF drift = mapToScene(viewportPos) - scenePos;
    const QTransform t = transform();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(drift.x() * t.m11()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(drift.y() * t.m22()));
    emit zoomChanged(target);
    viewport()->update();
}

bool BoardView::singleTool() const
{
    return m_tool == Tool::PinSingle || m_tool == Tool::HeaderSingle;
}

bool BoardView::multiTool() const
{
    return m_tool == Tool::PinMulti || m_tool == Tool::HeaderMulti;
}

bool BoardView::headerTool() const
{
    return m_tool == Tool::HeaderSingle || m_tool == Tool::HeaderMulti;
}

void BoardView::wheelEvent(QWheelEvent* event)
{
    // 多pin工具下 Ctrl+滚轮让给等距间距调节
    if (multiTool() && (event->modifiers() & Qt::ControlModifier)) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            setSpacing(m_spacing + (delta > 0 ? 2.0 : -2.0));
            emit spacingChanged(m_spacing);
        }
        event->accept();
        return;
    }
    if (event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }
    zoomAt(mapToScene(event->position().toPoint()),
           event->angleDelta().y() > 0 ? kZoomFactor : 1.0 / kZoomFactor);
    event->accept();
}

void BoardView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceDown)) {
        m_panning = true;
        m_lastPan = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_tool != Tool::Select) {
        const QPointF scenePos = mapToScene(event->position().toPoint());
        if (singleTool()) {
            placePin(scenePos);
        } else if (multiTool()) {
            if (!m_anchorSet) {
                m_anchor = scenePos;
                m_anchorSet = true;
            } else {
                commitMultiPins();
            }
            viewport()->update();
        } else if (m_tool == Tool::AppendDef) {
            const int id = m_boardScene->pinIdAt(scenePos);
            if (id > 0)
                emit appendRequested(id, scenePos);
        }
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void BoardView::placePin(const QPointF& scenePos)
{
    const QString kind = headerTool() ? QLatin1String(kKindHeader) : QLatin1String(kKindPin);
    const int id = m_boardScene->addPinAt(kind, scenePos);
    emit pinPlaced(id);
    viewport()->update();
}

void BoardView::commitMultiPins()
{
    int count = 0;
    const QList<QPointF> points = multiPreviewPoints(&count);
    if (points.isEmpty())
        return;
    const QString kind = headerTool() ? QLatin1String(kKindHeader) : QLatin1String(kKindPin);
    QList<int> ids;
    for (const QPointF& p : points)
        ids.append(m_boardScene->addPinAt(kind, p));
    m_anchorSet = false;
    // 延迟到事件循环：批量命名对话框可能在 press 处理栈内 exec()，避免嵌套事件循环重入
    QTimer::singleShot(0, this, [this, ids] { emit pinsPlaced(ids); });
    viewport()->update();
}

QList<QPointF> BoardView::multiPreviewPoints(int* count) const
{
    QList<QPointF> points;
    if (!m_anchorSet)
        return points;
    const qreal dy = m_cursor.y() - m_anchor.y();
    const int direction = dy < 0 ? -1 : 1;  // 光标在锚点上方 → 向上延伸
    const int n = std::clamp(int(std::abs(dy) / m_spacing) + 1, 1, kMultiMax);
    if (count)
        *count = n;
    for (int i = 0; i < n; ++i)
        points.append(QPointF(m_anchor.x(), m_anchor.y() + direction * i * m_spacing));
    return points;
}

void BoardView::mouseMoveEvent(QMouseEvent* event)
{
    m_cursor = mapToScene(event->position().toPoint());
    m_hasCursor = true;
    emit worldMouseMoved(m_cursor);
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPan;
        m_lastPan = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    if (m_tool != Tool::Select)
        viewport()->update();  // 十字光标/预览重绘
    QGraphicsView::mouseMoveEvent(event);
}

void BoardView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        viewport()->setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void BoardView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceDown = true;
        viewport()->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_anchorSet) {
            m_anchorSet = false;
            viewport()->update();
        } else {
            emit escapePressed();
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void BoardView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceDown = false;
        viewport()->setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void BoardView::leaveEvent(QEvent* event)
{
    m_hasCursor = false;
    viewport()->update();
    QGraphicsView::leaveEvent(event);
}

void BoardView::drawForeground(QPainter* painter, const QRectF& exposed)
{
    QGraphicsView::drawForeground(painter, exposed);
    if (m_tool == Tool::Select || !m_hasCursor)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 全画布十字虚线，交点 = 预览标记中心
    QPen cross(kCrosshair);
    cross.setStyle(Qt::DashLine);
    painter->setPen(cross);
    painter->drawLine(QPointF(exposed.left(), m_cursor.y()), QPointF(exposed.right(), m_cursor.y()));
    painter->drawLine(QPointF(m_cursor.x(), exposed.top()), QPointF(m_cursor.x(), exposed.bottom()));

    // 预览标记（圆 = 引脚，方 = 排针）
    QPen accent(kAccent);
    accent.setWidthF(1.8);
    painter->setPen(accent);
    painter->setBrush(Qt::NoBrush);
    auto drawMarker = [&](const QPointF& center) {
        if (headerTool())
            painter->drawRect(QRectF(center.x() - m_pinSize, center.y() - m_pinSize,
                                     2 * m_pinSize, 2 * m_pinSize));
        else
            painter->drawEllipse(center, m_pinSize, m_pinSize);
    };

    if (multiTool()) {
        int count = 0;
        const QList<QPointF> points = multiPreviewPoints(&count);
        for (const QPointF& p : points)
            drawMarker(p);
        if (m_anchorSet) {  // 状态气泡：间距 × 数量
            const QString label = tr("%1 个 × %2px").arg(count).arg(m_spacing);
            const QFontMetricsF fm(chipFont());
            const QSizeF bubbleSize(fm.horizontalAdvance(label) + 16.0, 22.0);
            const QRectF bubble(QRectF(m_cursor + QPointF(12, 12), bubbleSize));
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0x26, 0x32, 0x38, 220));
            painter->drawRoundedRect(bubble, 3, 3);
            painter->setPen(Qt::white);
            painter->setFont(chipFont());
            painter->drawText(bubble, Qt::AlignCenter, label);
        }
    } else if (singleTool()) {
        drawMarker(m_cursor);
    }
    painter->restore();
}

}  // namespace pinout
