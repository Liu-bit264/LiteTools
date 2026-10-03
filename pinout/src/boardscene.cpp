#include "boardscene.h"

#include "chiprender.h"
#include "pinitem.h"

#include <QGraphicsPixmapItem>
#include <QPainter>

namespace pinout {

BoardScene::BoardScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setBackgroundBrush(QColor(0xE8, 0xF0, 0xF7));
    connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        emit selectionChangedIds(selectedPinIds());
    });
}

void BoardScene::setProject(Project* project)
{
    m_project = project;
}

void BoardScene::setBoardPixmap(const QPixmap& pixmap)
{
    if (m_board) {
        removeItem(m_board);
        delete m_board;
    }
    m_board = addPixmap(pixmap);  // 板图固定在世界坐标 (0,0)
    m_board->setZValue(0.0);
    setSceneRect(-2000, -2000, pixmap.width() + 4000, pixmap.height() + 4000);
}

void BoardScene::setPinSize(qreal size)
{
    if (!m_project || qFuzzyCompare(m_project->pinSize, size))
        return;
    m_project->pinSize = size;
    for (PinItem* item : m_pins)
        item->setPinSize(size);
}

void BoardScene::rebuildAll()
{
    const QList<PinItem*> oldPins = m_pins.values();
    const QList<ChainItem*> oldChains = m_chains.values();
    {  // 图元拆除期间 selectionChanged 会同步广播，避免面板读到中间态
        const QSignalBlocker blocker(this);
        for (PinItem* item : oldPins) {
            removeItem(item);
            delete item;
        }
        for (ChainItem* item : oldChains) {
            removeItem(item);
            delete item;
        }
    }
    m_pins.clear();
    m_chains.clear();
    if (!m_project)
        return;
    for (const Pin& p : m_project->pins) {
        auto* pinItem = new PinItem(p.id);
        pinItem->setPos(p.pos);
        pinItem->setKind(p.kind);
        pinItem->setPinSize(m_project->pinSize);
        addItem(pinItem);
        connect(pinItem, &PinItem::moved, this, &BoardScene::onPinItemMoved);
        m_pins.insert(p.id, pinItem);
        refreshPinItemStyle(p.id);  // 必须在注册进 m_pins 之后（内部按 id 查表）

        auto* chainItem = new ChainItem();
        addItem(chainItem);
        m_chains.insert(p.id, chainItem);
        relayoutPin(p.id);
    }
    emit selectionChangedIds(selectedPinIds());  // 重建后统一同步一次（现为空选中）
}

void BoardScene::refreshPinStyle(int id)
{
    refreshPinItemStyle(id);
}

void BoardScene::relayoutPin(int id)
{
    if (!m_project)
        return;
    ChainItem* chain = m_chains.value(id);
    const Pin* pin = m_project->findPin(id);
    if (!chain || !pin)
        return;
    const QFontMetricsF fm(chipFont());
    chain->setChain(layoutChain(*m_project, *pin, fm,
                                QSizeF(m_project->imageSize.width(), m_project->imageSize.height())));
}

int BoardScene::addPinAt(const QString& kind, const QPointF& pos, const QString& name)
{
    if (!m_project)
        return 0;
    Pin pin;
    pin.id = m_project->nextPinId();
    pin.kind = kind;
    pin.pos = pos;
    pin.name = name;
    pin.side = Project::guessSide(pos.x(), m_project->imageSize.width());
    pin.category = Project::guessCategory(name);
    m_project->pins.append(pin);

    auto* pinItem = new PinItem(pin.id);
    pinItem->setPos(pos);
    pinItem->setKind(kind);
    pinItem->setPinSize(m_project->pinSize);
    addItem(pinItem);
    connect(pinItem, &PinItem::moved, this, &BoardScene::onPinItemMoved);
    m_pins.insert(pin.id, pinItem);
    refreshPinItemStyle(pin.id);  // 必须在注册进 m_pins 之后（内部按 id 查表）

    auto* chainItem = new ChainItem();
    addItem(chainItem);
    m_chains.insert(pin.id, chainItem);
    relayoutPin(pin.id);

    emit pinModified();
    return pin.id;
}

void BoardScene::deletePins(const QList<int>& ids)
{
    if (!m_project)
        return;
    bool changed = false;
    {
        const QSignalBlocker blocker(this);  // 拆除期间不向面板广播中间态
        for (int id : ids) {
            if (PinItem* item = m_pins.value(id)) {
                removeItem(item);
                delete item;
                m_pins.remove(id);
            }
            if (ChainItem* chain = m_chains.value(id)) {
                removeItem(chain);
                delete chain;
                m_chains.remove(id);
            }
            m_project->removePin(id);
            changed = true;
        }
    }
    if (changed) {
        emit selectionChangedIds(selectedPinIds());
        emit pinModified();
    }
}

void BoardScene::applyBatchNaming(const QList<int>& ids, const QString& prefix, int start, int step)
{
    if (!m_project)
        return;
    for (int i = 0; i < ids.size(); ++i) {
        if (Pin* pin = m_project->findPin(ids[i])) {
            pin->name = prefix + QString::number(start + i * step);
            if (PinItem* item = m_pins.value(ids[i]))
                refreshPinItemStyle(ids[i]);
            relayoutPin(ids[i]);
        }
    }
    emit pinModified();
}

QList<int> BoardScene::selectedPinIds() const
{
    QList<int> ids;
    const QList<QGraphicsItem*> sel = selectedItems();
    for (QGraphicsItem* item : sel) {
        if (auto* pin = qgraphicsitem_cast<PinItem*>(item))
            ids.append(pin->pinId());
    }
    return ids;
}

void BoardScene::selectPins(const QList<int>& ids)
{
    const QSignalBlocker blocker(this);  // 不回灌 selectionChangedIds
    for (auto it = m_pins.constBegin(); it != m_pins.constEnd(); ++it)
        it.value()->setSelected(ids.contains(it.key()));
}

int BoardScene::pinIdAt(const QPointF& scenePos) const
{
    const QList<QGraphicsItem*> hits = items(scenePos);
    for (QGraphicsItem* item : hits) {
        if (auto* pin = qgraphicsitem_cast<PinItem*>(item))
            return pin->pinId();
    }
    return 0;
}

QRectF BoardScene::boardRect() const
{
    if (!m_project)
        return QRectF();
    return QRectF(0, 0, m_project->imageSize.width(), m_project->imageSize.height());
}

void BoardScene::setPinsInteractive(bool interactive)
{
    for (PinItem* item : m_pins) {
        item->setFlag(QGraphicsItem::ItemIsMovable, interactive);
        item->setFlag(QGraphicsItem::ItemIsSelectable, interactive);
    }
}

void BoardScene::onPinItemMoved(int id, const QPointF& scenePos)
{
    if (!m_project)
        return;
    if (Pin* pin = m_project->findPin(id)) {
        pin->pos = scenePos;
        relayoutPin(id);
        emit pinModified();
    }
}

void BoardScene::refreshPinItemStyle(int id)
{
    if (!m_project)
        return;
    if (PinItem* item = m_pins.value(id)) {
        const Pin* pin = m_project->findPin(id);
        if (!pin)
            return;
        auto it = m_project->categories.constFind(pin->category);
        item->setCategoryColor(it == m_project->categories.constEnd()
                                   ? QColor(0x8E, 0x24, 0xAA)
                                   : it->color);
        item->setKind(pin->kind);
    }
}

void BoardScene::drawBackground(QPainter* painter, const QRectF& exposed)
{
    QGraphicsScene::drawBackground(painter, exposed);
    painter->setRenderHint(QPainter::Antialiasing, true);
    // LOD：缩放很深时跳过 24px 小格只画大格（导出路径 scale≥1 不受影响）
    const qreal px = painter->transform().m11();
    if (px * 24.0 >= 6.0)
        drawGrid(*painter, exposed);
    else
        drawGrid(*painter, exposed, 120.0, 600.0);
}

}  // namespace pinout
