#pragma once
// 画布场景：蓝图网格背景 + 居中板图 + 引脚标记/芯片链图元管理。
// 模型（Project）的唯一写入口都经过这里，保证图元与模型同步。

#include "project.h"

#include <QGraphicsScene>
#include <QMap>

class QGraphicsPixmapItem;

namespace pinout {

class PinItem;
class ChainItem;

class BoardScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit BoardScene(QObject* parent = nullptr);

    void setProject(Project* project);  // 非拥有
    Project* project() const { return m_project; }
    void setBoardPixmap(const QPixmap& pixmap);
    void setPinSize(qreal size);

    // 全量重建图元（打开工程/分类配色/侧别批量变化后）
    void rebuildAll();
    // 单个引脚的芯片链重排（移动/追加定义/改名后）
    void relayoutPin(int id);
    // 单个引脚标记的样式刷新（分类颜色/种类变化后，供主窗口定向调用）
    void refreshPinStyle(int id);

    int addPinAt(const QString& kind, const QPointF& pos, const QString& name = QString());
    void deletePins(const QList<int>& ids);
    void applyBatchNaming(const QList<int>& ids, const QString& prefix, int start, int step);

    QList<int> selectedPinIds() const;
    void selectPins(const QList<int>& ids);
    int pinIdAt(const QPointF& scenePos) const;
    QRectF boardRect() const;

    // 放置/追加工具下冻结图元拖动，避免误交互
    void setPinsInteractive(bool interactive);

signals:
    void pinModified();                    // 任何模型变化（外部标脏）
    void selectionChangedIds(const QList<int>& ids);

public slots:
    void onPinItemMoved(int id, const QPointF& scenePos);

protected:
    void drawBackground(QPainter* painter, const QRectF& exposed) override;

private:
    void refreshPinItemStyle(int id);

    Project* m_project = nullptr;
    QGraphicsPixmapItem* m_board = nullptr;
    QMap<int, PinItem*> m_pins;
    QMap<int, ChainItem*> m_chains;
};

}  // namespace pinout
