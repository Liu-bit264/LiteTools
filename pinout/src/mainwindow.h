#pragma once
// 主窗口：JLC 风格工具栏 + 画布 + 右侧属性面板 + 状态栏。
// 模型写入口集中在此：场景图元事件与面板信号都汇到这里落库。

#include "boardview.h"
#include "project.h"

#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QSpinBox>

class QActionGroup;
class QCloseEvent;

namespace pinout {

class BoardScene;
class PropsPanel;
class AppendDefPopup;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openProject(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;  // 首次显示后再适应窗口（此前视口尺寸未定）

private slots:
    void onNewProject();
    void onOpenProject();
    bool onSave();
    void onExport();
    void onLibraryManager();
    void onHelp();

    void onMouseMoved(const QPointF& scenePos);
    void onZoomChanged(qreal factor);
    void onSpacingEdited(double value);
    void onEscape();

    void onPinPlaced(int id);
    void onPinsPlaced(const QList<int>& ids);
    void onAppendRequested(int pinId, const QPointF& scenePos);
    void onDefsAppend(const QString& text, const QString& categoryId);
    void onDefRemove(int pinId, const QString& text);
    void onNameEdited(int pinId, const QString& name);
    void onBatchSide(const QString& side);
    void onBatchCategory(const QString& categoryId);
    void onDeleteSelection();

    void onFit();
    void onZoom100();

private:
    void buildUi();
    void setTool(BoardView::Tool tool);
    void markDirty();
    bool confirmDiscard();
    void updateTitle();
    void refreshProps();
    void onDefsAppendTo(const QList<int>& ids, const QString& text, const QString& category);

    std::shared_ptr<Project> m_project;
    BoardScene* m_scene = nullptr;
    BoardView* m_view = nullptr;
    PropsPanel* m_props = nullptr;
    AppendDefPopup* m_appendPopup = nullptr;
    int m_appendTarget = 0;

    QActionGroup* m_toolGroup = nullptr;
    QAction* m_actSelect = nullptr;
    QAction* m_actPinSingle = nullptr;
    QAction* m_actPinMulti = nullptr;
    QAction* m_actHeaderSingle = nullptr;
    QAction* m_actHeaderMulti = nullptr;
    QAction* m_actAppendDef = nullptr;
    QSpinBox* m_spacingSpin = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    bool m_dirty = false;
    bool m_fitted = false;
    QString m_lastNamePrefix;
};

}  // namespace pinout
