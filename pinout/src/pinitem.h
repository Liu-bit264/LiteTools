#pragma once
// 引脚图元：PinItem = 圆/方标记（可选中、可拖动，交互入口）；
// ChainItem = 名称/定义芯片链（纯绘制，非交互，随引脚重排）。
// 二者分离：橡皮筋框选与拖动只作用于标记，芯片链是派生渲染。

#include "layout.h"

#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QPainter>

namespace pinout {

class PinItem : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 1 };  // 供 qgraphicsitem_cast 精确区分（ChainItem 为 +2）

    explicit PinItem(int pinId, QGraphicsItem* parent = nullptr);

    int pinId() const { return m_pinId; }
    int type() const override { return Type; }
    void setKind(const QString& kind);
    void setCategoryColor(const QColor& color);
    void setPinSize(qreal size);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    void moved(int pinId, const QPointF& scenePos);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    int m_pinId;
    bool m_header = false;
    QColor m_color{0x8E, 0x24, 0xAA};
    qreal m_pinSize = 7.0;
};

class ChainItem : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };

    ChainItem()
    {
        setAcceptedMouseButtons(Qt::NoButton);  // 纯派生渲染，不参与交互
        setZValue(0.5);
    }

    int type() const override { return Type; }

    void setChain(const ChainLayout& chain);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    ChainLayout m_chain;
};

}  // namespace pinout
