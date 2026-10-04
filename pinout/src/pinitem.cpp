#include "pinitem.h"

#include "chiprender.h"

#include <QPainter>

namespace pinout {

PinItem::PinItem(int pinId, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_pinId(pinId)
{
    setFlag(ItemIsSelectable, true);
    setFlag(ItemIsMovable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setZValue(1.0);
}

void PinItem::setKind(const QString& kind)
{
    m_header = (kind == QLatin1String(kKindHeader));
    update();
}

void PinItem::setCategoryColor(const QColor& color)
{
    m_color = color;
    update();
}

void PinItem::setPinSize(qreal size)
{
    m_pinSize = size;
    prepareGeometryChange();
}

QRectF PinItem::boundingRect() const
{
    return QRectF(-m_pinSize - 2.0, -m_pinSize - 2.0, 2 * m_pinSize + 4.0, 2 * m_pinSize + 4.0);
}

void PinItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    QColor fill = m_color;
    fill.setAlpha(190);
    QPen pen(QColor(0x37, 0x47, 0x4F));
    pen.setWidthF(1.6);
    if (isSelected())
        pen.setColor(QColor(0x1E, 0x88, 0xE5));  // 选中高亮
    painter->setPen(pen);
    painter->setBrush(fill);
    if (m_header)
        painter->drawRect(QRectF(-m_pinSize, -m_pinSize, 2 * m_pinSize, 2 * m_pinSize));
    else
        painter->drawEllipse(QPointF(0, 0), m_pinSize, m_pinSize);
}

QVariant PinItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged)
        emit moved(m_pinId, value.toPointF());
    return QGraphicsObject::itemChange(change, value);
}

void ChainItem::setChain(const ChainLayout& chain)
{
    prepareGeometryChange();
    m_chain = chain;
    update();
}

QRectF ChainItem::boundingRect() const
{
    return m_chain.bounds;
}

void ChainItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    drawChain(*painter, m_chain);
}

}  // namespace pinout
