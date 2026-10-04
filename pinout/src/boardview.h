#pragma once
// 画布视图：工具状态机 + 鼠标/键盘交互 + 十字光标与放置预览（drawForeground）。
// 视图只做交互编排，模型写入全部经由 BoardScene。

#include "boardscene.h"

#include <QGraphicsView>

namespace pinout {

class BoardView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, PinSingle, PinMulti, HeaderSingle, HeaderMulti, AppendDef };
    Q_ENUM(Tool)

    explicit BoardView(BoardScene* scene, QWidget* parent = nullptr);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setSpacing(qreal spacing);
    void setPinSize(qreal size);
    void fitBoard();
    void zoom100();
    void zoomStepIn();   // 菜单/快捷键步进放大
    void zoomStepOut();

signals:
    void worldMouseMoved(const QPointF& scenePos);
    void zoomChanged(qreal factor);
    void spacingChanged(qreal spacing);
    void pinPlaced(int id);
    void pinsPlaced(const QList<int>& ids);   // 多针提交后（供批量命名）
    void appendRequested(int pinId, const QPointF& scenePos);
    void escapePressed();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& exposed) override;

private:
    void zoomAt(const QPointF& scenePos, qreal factor);
    bool singleTool() const;
    bool multiTool() const;
    bool headerTool() const;
    void placePin(const QPointF& scenePos);
    void commitMultiPins();
    QList<QPointF> multiPreviewPoints(int* count) const;

    BoardScene* m_boardScene;
    Tool m_tool = Tool::Select;
    qreal m_spacing = 24.0;
    qreal m_pinSize = 7.0;
    bool m_hasCursor = false;
    QPointF m_cursor;
    bool m_anchorSet = false;  // 多pin工具：已定锚点，等待第二次点击
    QPointF m_anchor;
    bool m_panning = false;    // 中键/空格+左键 平移
    QPoint m_lastPan;
    bool m_spaceDown = false;
};

}  // namespace pinout
