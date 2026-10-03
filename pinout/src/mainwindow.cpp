#include "mainwindow.h"

#include "boardscene.h"
#include "chiprender.h"
#include "exporter.h"
#include "librarydlg.h"
#include "propspanel.h"
#include "startpage.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QCompleter>
#include <QCoreApplication>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringListModel>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

namespace pinout {

namespace {
const char* kImageFilter = "图片 (*.png *.jpg *.jpeg *.bmp *.gif);;所有文件 (*)";
const char* kProjectFilter = "pinout 工程 (*.pinout.json);;所有文件 (*)";
const char* kPngFilter = "PNG 图片 (*.png)";

QString toolHint(BoardView::Tool tool)
{
    switch (tool) {
        case BoardView::Tool::Select:
            return MainWindow::tr("选择：框选/点选引脚后拖动；Del 删除；滚轮缩放，中键或空格+左键平移");
        case BoardView::Tool::PinSingle:
            return MainWindow::tr("单pin引脚：十字虚线中心即圆心，单击落针，Esc 返回选择");
        case BoardView::Tool::PinMulti:
            return MainWindow::tr("多pin引脚：单击定锚点 → 向上/下移动决定数量 → Ctrl+滚轮调间距 → 再次单击确认");
        case BoardView::Tool::HeaderSingle:
            return MainWindow::tr("单pin排针：十字虚线中心即方标记中心，单击落针，Esc 返回选择");
        case BoardView::Tool::HeaderMulti:
            return MainWindow::tr("多pin排针：单击定锚点 → 向上/下移动决定数量 → Ctrl+滚轮调间距 → 再次单击确认");
        case BoardView::Tool::AppendDef:
            return MainWindow::tr("追加复用定义：单击引脚弹出输入框，输入或从复用库选择定义，Enter 追加");
    }
    return {};
}

}  // namespace

// ==================== 追加复用定义弹层 ====================
// 工具⑤的交互主体：点击引脚弹出；输入新文本（自动入复用库）或下拉补全已有定义；
// Enter = 追加一枚定义芯片（可连续追加）；Esc / 点击弹层外 = 结束。
class AppendDefPopup : public QWidget {
    Q_OBJECT
public:
    explicit AppendDefPopup(QWidget* parent = nullptr)
        : QWidget(parent, Qt::Popup)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        auto* hint = new QLabel(tr("<b>追加复用定义</b><br/>输入新定义或从复用库选择，"
                                   "Enter 追加；Esc / 点击空白处结束"));
        hint->setStyleSheet(QStringLiteral("font-size: 11px; color: #556;"));
        layout->addWidget(hint);
        m_text = new QComboBox;
        m_text->setEditable(true);
        m_text->setInsertPolicy(QComboBox::NoInsert);
        m_text->lineEdit()->setPlaceholderText(tr("如 USART1_TX / FT"));
        m_text->setMinimumWidth(240);
        layout->addWidget(m_text);
        m_category = new QComboBox;
        layout->addWidget(m_category);
        auto* ok = new QPushButton(tr("追加（Enter）"));
        layout->addWidget(ok);
        // 复用同一个 QCompleter（setProject 只更新其 model，避免每次点击引脚泄漏旧实例）
        m_completer = new QCompleter(m_text);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setFilterMode(Qt::MatchContains);
        m_text->setCompleter(m_completer);
        connect(ok, &QPushButton::clicked, this, &AppendDefPopup::emitAppend);
        connect(m_text->lineEdit(), &QLineEdit::returnPressed, this, &AppendDefPopup::emitAppend);
        connect(m_text->lineEdit(), &QLineEdit::textEdited, this, [this](const QString& text) {
            syncCategoryToLibrary(text);
        });
    }

    void setProject(Project* project)
    {
        m_project = project;
        m_category->clear();
        for (const QString& id : m_project->orderedCategoryIds()) {
            const CategoryDef& c = m_project->categories[id];
            QPixmap swatch(12, 12);
            swatch.fill(c.color);
            m_category->addItem(QIcon(swatch), c.label, id);
        }
        QStringList texts;
        for (const LibraryEntry& e : m_project->library)
            texts << e.text;
        auto* model = qobject_cast<QStringListModel*>(m_completer->model());
        if (!model) {
            model = new QStringListModel(m_completer);
            m_completer->setModel(model);
        }
        model->setStringList(texts);
    }

    void openAt(const QPoint& globalPos)
    {
        m_text->setCurrentText(QString());
        move(globalPos);
        show();
        raise();
        m_text->lineEdit()->setFocus();
    }

signals:
    void appended(const QString& text, const QString& categoryId);

private slots:
    void emitAppend()
    {
        const QString text = m_text->currentText().trimmed();
        if (text.isEmpty())
            return;
        emit appended(text, m_category->currentData().toString());
        m_text->setCurrentText(QString());
        m_text->lineEdit()->setFocus();
    }

private:
    void syncCategoryToLibrary(const QString& text)
    {
        // 复用联动：输入命中库条目时自动切到其分类
        for (const LibraryEntry& e : m_project->library) {
            if (e.text.compare(text, Qt::CaseInsensitive) == 0) {
                const int idx = m_category->findData(e.category);
                if (idx >= 0)
                    m_category->setCurrentIndex(idx);
                return;
            }
        }
    }

    Project* m_project = nullptr;
    QComboBox* m_text;
    QComboBox* m_category;
    QCompleter* m_completer;
};

// ==================== 主窗口 ====================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    updateTitle();
}

void MainWindow::buildUi()
{
    m_scene = new BoardScene(this);
    m_view = new BoardView(m_scene, this);
    setCentralWidget(m_view);
    m_props = new PropsPanel;
    auto* dock = new QDockWidget(tr("属性"), this);
    dock->setWidget(m_props);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // ---- 工具条 ----
    auto* toolBar = addToolBar(tr("工具"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    auto addToolAction = [this, toolBar](const QString& text, BoardView::Tool tool, Qt::Key key) {
        QAction* act = toolBar->addAction(text);
        act->setCheckable(true);
        act->setShortcut(QKeySequence(key));
        connect(act, &QAction::triggered, this, [this, tool]() { setTool(tool); });
        m_toolGroup->addAction(act);
        return act;
    };
    m_actSelect = addToolAction(tr("↖ 选择 (V)"), BoardView::Tool::Select, Qt::Key_V);
    m_actPinSingle = addToolAction(tr("● 单pin引脚 (1)"), BoardView::Tool::PinSingle, Qt::Key_1);
    m_actPinMulti = addToolAction(tr("⁞● 多pin引脚 (2)"), BoardView::Tool::PinMulti, Qt::Key_2);
    m_actHeaderSingle = addToolAction(tr("▢ 单pin排针 (3)"), BoardView::Tool::HeaderSingle, Qt::Key_3);
    m_actHeaderMulti = addToolAction(tr("⁞▢ 多pin排针 (4)"), BoardView::Tool::HeaderMulti, Qt::Key_4);
    m_actAppendDef = addToolAction(tr("⧉ 追加复用定义 (5)"), BoardView::Tool::AppendDef, Qt::Key_5);
    m_actSelect->setChecked(true);

    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(tr(" 间距"), toolBar));
    m_spacingSpin = new QSpinBox(toolBar);
    m_spacingSpin->setRange(4, 200);
    m_spacingSpin->setValue(24);
    m_spacingSpin->setSuffix(tr(" px"));
    m_spacingSpin->setToolTip(tr("多pin等距间距（放置中 Ctrl+滚轮 可调）"));
    toolBar->addWidget(m_spacingSpin);

    // ---- 菜单 ----
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("新建工程…"), this, &MainWindow::onNewProject);
    fileMenu->addAction(tr("打开工程…"), this, &MainWindow::onOpenProject, QKeySequence::Open);
    fileMenu->addAction(tr("保存"), this, &MainWindow::onSave, QKeySequence::Save);
    fileMenu->addAction(tr("导出 PNG…"), this, &MainWindow::onExport, QKeySequence(QStringLiteral("Ctrl+E")));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出"), qApp, &QCoreApplication::quit, QKeySequence::Quit);

    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("放大"), m_view, &BoardView::zoomStepIn, QKeySequence::ZoomIn);
    viewMenu->addAction(tr("缩小"), m_view, &BoardView::zoomStepOut, QKeySequence::ZoomOut);
    viewMenu->addAction(tr("适应窗口"), this, &MainWindow::onFit, QKeySequence(Qt::Key_W));
    viewMenu->addAction(tr("100%"), this, &MainWindow::onZoom100, QKeySequence(QStringLiteral("Ctrl+0")));

    QMenu* toolMenu = menuBar()->addMenu(tr("工具(&T)"));
    for (QAction* act : {m_actSelect, m_actPinSingle, m_actPinMulti, m_actHeaderSingle,
                         m_actHeaderMulti, m_actAppendDef})
        toolMenu->addAction(act);
    toolMenu->addSeparator();
    toolMenu->addAction(tr("复用定义库管理…"), this, &MainWindow::onLibraryManager);

    QMenu* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("使用说明"), this, &MainWindow::onHelp, QKeySequence(Qt::Key_F1));

    // ---- 状态栏 ----
    m_hintLabel = new QLabel(toolHint(BoardView::Tool::Select));
    m_coordLabel = new QLabel(tr("X: - Y: -"));
    m_coordLabel->setMinimumWidth(150);
    m_zoomLabel = new QLabel(tr("100%"));
    m_zoomLabel->setMinimumWidth(56);
    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    // ---- 全局快捷键 ----
    auto* delShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(delShortcut, &QShortcut::activated, this, &MainWindow::onDeleteSelection);

    // ---- 信号接线 ----
    connect(m_scene, &BoardScene::pinModified, this, &MainWindow::markDirty);
    connect(m_scene, &BoardScene::selectionChangedIds, m_props, &PropsPanel::setSelection);
    connect(m_view, &BoardView::worldMouseMoved, this, &MainWindow::onMouseMoved);
    connect(m_view, &BoardView::zoomChanged, this, &MainWindow::onZoomChanged);
    // qreal → int 显式经 qRound，避免 PMF 连接静默截断造成的间距漂移
    connect(m_view, &BoardView::spacingChanged, this,
            [this](qreal spacing) { m_spacingSpin->setValue(qRound(spacing)); });
    connect(m_view, &BoardView::pinPlaced, this, &MainWindow::onPinPlaced);
    connect(m_view, &BoardView::pinsPlaced, this, &MainWindow::onPinsPlaced);
    connect(m_view, &BoardView::appendRequested, this, &MainWindow::onAppendRequested);
    connect(m_view, &BoardView::escapePressed, this, &MainWindow::onEscape);
    connect(m_spacingSpin, &QSpinBox::valueChanged, this, &MainWindow::onSpacingEdited);

    connect(m_props, &PropsPanel::nameEdited, this, &MainWindow::onNameEdited);
    connect(m_props, &PropsPanel::batchSideChanged, this, &MainWindow::onBatchSide);
    connect(m_props, &PropsPanel::batchCategoryChanged, this, &MainWindow::onBatchCategory);
    connect(m_props, &PropsPanel::defsAppendRequested, this, &MainWindow::onDefsAppend);
    connect(m_props, &PropsPanel::defRemoveRequested, this, &MainWindow::onDefRemove);
    connect(m_props, &PropsPanel::libraryManagerRequested, this, &MainWindow::onLibraryManager);
}

bool MainWindow::openProject(const QString& path)
{
    QString err;
    auto project = Project::load(path, &err);
    if (!project) {
        QMessageBox::warning(this, tr("打开失败"), err);
        return false;
    }
    QPixmap board(project->absoluteImagePath());
    bool reSpecified = false;
    if (board.isNull()) {
        const QString img = QFileDialog::getOpenFileName(this, tr("重新指定开发板图片"),
                                                         QString(), QLatin1String(kImageFilter));
        if (img.isEmpty())
            return false;
        board.load(img);
        if (board.isNull()) {
            QMessageBox::warning(this, tr("图片不可用"), tr("无法读取图片：%1").arg(img));
            return false;
        }
        project->imagePath = img;
        reSpecified = true;
    }
    if (reSpecified || project->imageSize.isEmpty() || project->imageSize != board.size()) {
        project->imageSize = board.size();
        if (!project->save(&err))  // 图片变更必须落盘，否则每次打开都要重选
            QMessageBox::warning(this, tr("工程保存失败"), err);
    }

    if (m_appendPopup) {
        m_appendPopup->deleteLater();  // 切工程先销毁弹层，避免其内部 Project* 悬空
        m_appendPopup = nullptr;
    }
    m_project = project;
    m_scene->setProject(m_project.get());
    m_scene->setBoardPixmap(board);
    m_scene->rebuildAll();
    m_props->setProject(m_project.get());
    m_view->setPinSize(m_project->pinSize);
    m_spacingSpin->blockSignals(true);
    m_spacingSpin->setValue(qRound(m_project->spacing));
    m_spacingSpin->blockSignals(false);
    m_view->setSpacing(m_project->spacing);
    m_view->fitBoard();
    setTool(BoardView::Tool::Select);
    m_dirty = false;
    updateTitle();
    StartPage::pushRecent(m_project->filePath);
    return true;
}

void MainWindow::setTool(BoardView::Tool tool)
{
    m_view->setTool(tool);
    if (m_appendPopup)
        m_appendPopup->close();
    QAction* target = nullptr;
    switch (tool) {
        case BoardView::Tool::Select: target = m_actSelect; break;
        case BoardView::Tool::PinSingle: target = m_actPinSingle; break;
        case BoardView::Tool::PinMulti: target = m_actPinMulti; break;
        case BoardView::Tool::HeaderSingle: target = m_actHeaderSingle; break;
        case BoardView::Tool::HeaderMulti: target = m_actHeaderMulti; break;
        case BoardView::Tool::AppendDef: target = m_actAppendDef; break;
    }
    if (target && !target->isChecked())
        target->setChecked(true);  // Esc 返回选择时同步工具组勾选
    m_hintLabel->setText(toolHint(tool));
}

void MainWindow::markDirty()
{
    if (m_dirty)
        return;
    m_dirty = true;
    updateTitle();
}

bool MainWindow::confirmDiscard()
{
    if (!m_dirty)
        return true;
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, tr("未保存的修改"),
        tr("当前工程有未保存的修改，是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save)
        return onSave();  // 保存失败则不放行关闭，避免静默丢数据
    return ret != QMessageBox::Cancel;
}

void MainWindow::updateTitle()
{
    const QString name = m_project ? QFileInfo(m_project->filePath).fileName()
                                   : tr("未打开工程");
    setWindowTitle(tr("LiteTools · pinout — %1%2").arg(name, m_dirty ? QStringLiteral(" *") : QString()));
}

void MainWindow::refreshProps()
{
    m_props->refreshAll();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscard())
        event->accept();
    else
        event->ignore();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_fitted) {  // 视口尺寸就绪后做首次适应窗口
        m_fitted = true;
        m_view->fitBoard();
    }
}

// ---- 文件 ----

void MainWindow::onOpenProject()
{
    if (!confirmDiscard())
        return;
    StartPage page(this);
    if (page.exec() == QDialog::Accepted && !page.selectedProject().isEmpty())
        openProject(page.selectedProject());
}

void MainWindow::onNewProject()
{
    if (!confirmDiscard())
        return;
    StartPage page(this);
    page.exec();  // 用户在起始页里点"新建工程"
    if (!page.selectedProject().isEmpty())
        openProject(page.selectedProject());
}

bool MainWindow::onSave()
{
    if (!m_project)
        return true;
    QString err;
    if (!m_project->save(&err)) {
        QMessageBox::warning(this, tr("保存失败"), err);
        return false;
    }
    m_dirty = false;
    updateTitle();
    return true;
}

void MainWindow::onExport()
{
    if (!m_project)
        return;
    const QString def = QFileInfo(m_project->filePath).absolutePath() + QLatin1Char('/')
                        + QFileInfo(m_project->filePath).completeBaseName() + QStringLiteral(".png");
    const QString out = QFileDialog::getSaveFileName(this, tr("导出 PNG"), def,
                                                     QLatin1String(kPngFilter));
    if (out.isEmpty())
        return;
    QString err;
    if (Exporter::exportPng(*m_project, out, Exporter::kDefaultScale, &err))
        QMessageBox::information(this, tr("导出成功"), tr("已导出：%1").arg(out));
    else
        QMessageBox::warning(this, tr("导出失败"), err);
}

// ---- 画布联动 ----

void MainWindow::onMouseMoved(const QPointF& pos)
{
    m_coordLabel->setText(tr("X: %1 Y: %2").arg(pos.x(), 0, 'f', 1).arg(pos.y(), 0, 'f', 1));
}

void MainWindow::onZoomChanged(qreal factor)
{
    m_zoomLabel->setText(tr("%1%").arg(qRound(factor * 100)));
}

void MainWindow::onSpacingEdited(double value)
{
    m_view->setSpacing(value);
    if (m_project) {
        m_project->spacing = value;
        markDirty();  // 间距是工程级属性，改动不标脏会在关窗时静默丢失
    }
}

void MainWindow::onEscape()
{
    setTool(BoardView::Tool::Select);
}

void MainWindow::onFit()
{
    m_view->fitBoard();
}

void MainWindow::onZoom100()
{
    m_view->zoom100();
}

// ---- 引脚编辑 ----

void MainWindow::onPinPlaced(int id)
{
    m_scene->selectPins({id});
    m_props->setSelection({id});  // selectPins 有 QSignalBlocker，需显式同步面板选中态
    m_props->focusName();
}

void MainWindow::onPinsPlaced(const QList<int>& ids)
{
    if (ids.isEmpty())
        return;
    // 批量命名：前缀 + 起始序号 + 步进（全部可留空跳过）
    QDialog dlg(this);
    dlg.setWindowTitle(tr("批量命名"));
    auto* form = new QFormLayout(&dlg);
    auto* prefix = new QLineEdit(m_lastNamePrefix);
    auto* start = new QSpinBox;
    start->setRange(0, 999999);
    auto* step = new QSpinBox;
    step->setRange(1, 100);
    step->setValue(1);
    form->addRow(tr("前缀"), prefix);
    form->addRow(tr("起始序号"), start);
    form->addRow(tr("步进"), step);
    auto* note = new QLabel(tr("留空前缀 = 保持未命名（可稍后在属性面板填写）"));
    form->addRow(note);
    auto* btns = new QHBoxLayout;
    auto* ok = new QPushButton(tr("应用"));
    auto* skip = new QPushButton(tr("跳过"));
    btns->addStretch(1);
    btns->addWidget(ok);
    btns->addWidget(skip);
    form->addRow(btns);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(skip, &QPushButton::clicked, &dlg, &QDialog::reject);

    m_scene->selectPins(ids);
    if (dlg.exec() == QDialog::Accepted) {
        m_lastNamePrefix = prefix->text().trimmed();
        m_scene->applyBatchNaming(ids, m_lastNamePrefix, start->value(), step->value());
        if (!m_lastNamePrefix.isEmpty()) {
            // 命名后按端口习惯纠偏分类（仅限仍为特殊功能/端口组的），一次重建
            bool changed = false;
            for (int id : ids) {
                Pin* pin = m_project->findPin(id);
                const QString old = pin ? pin->category : QString();
                if (pin && (old == QStringLiteral("special") || old == QStringLiteral("pa")
                            || old == QStringLiteral("pb") || old == QStringLiteral("pc"))) {
                    const QString guessed = Project::guessCategory(pin->name);
                    if (guessed != old) {
                        pin->category = guessed;
                        changed = true;
                    }
                }
            }
            if (changed)
                m_scene->rebuildAll();
        }
    }
    refreshProps();
    m_props->setSelection(ids);  // 同上：程序化选中必须显式同步，防止改名命中旧引脚
    m_props->focusName();
}

void MainWindow::onAppendRequested(int pinId, const QPointF& scenePos)
{
    m_appendTarget = pinId;
    if (!m_appendPopup) {
        m_appendPopup = new AppendDefPopup(this);
        connect(m_appendPopup, &AppendDefPopup::appended, this,
                [this](const QString& text, const QString& category) {
                    // 弹层一次只服务一个目标引脚
                    const QList<int> ids{m_appendTarget};
                    onDefsAppendTo(ids, text, category);
                });
    }
    m_appendPopup->setProject(m_project.get());
    const QPoint vp = m_view->mapFromScene(scenePos);
    m_appendPopup->openAt(m_view->viewport()->mapToGlobal(vp) + QPoint(16, -16));
}

void MainWindow::onDefsAppend(const QString& text, const QString& category)
{
    onDefsAppendTo(m_scene->selectedPinIds(), text, category);
    refreshProps();
}

void MainWindow::onDefsAppendTo(const QList<int>& ids, const QString& text, const QString& category)
{
    if (!m_project || ids.isEmpty() || text.isEmpty())
        return;
    bool changed = false;
    for (int id : ids) {
        Pin* pin = m_project->findPin(id);
        if (!pin || pin->defs.contains(text))
            continue;  // 同一引脚不重复追加同文本
        pin->defs.append(text);
        m_scene->relayoutPin(id);
        changed = true;
    }
    if (changed) {
        m_project->upsertLibrary(text, category);  // 复用库自动收录
        markDirty();
        refreshProps();
    }
}

void MainWindow::onDefRemove(int pinId, const QString& text)
{
    Pin* pin = m_project ? m_project->findPin(pinId) : nullptr;
    if (!pin || !pin->defs.contains(text))
        return;
    pin->defs.removeAll(text);
    m_scene->relayoutPin(pinId);
    markDirty();
    refreshProps();
}

void MainWindow::onNameEdited(int pinId, const QString& name)
{
    Pin* pin = m_project ? m_project->findPin(pinId) : nullptr;
    if (!pin)
        return;
    const QString old = pin->category;
    pin->name = name;
    // 名称落到端口命名习惯时自动纠偏分类（用户仍可在面板改回）
    if (old == QStringLiteral("pa") || old == QStringLiteral("pb")
        || old == QStringLiteral("pc") || old == QStringLiteral("special"))
        pin->category = Project::guessCategory(name);
    m_scene->relayoutPin(pinId);
    m_scene->refreshPinStyle(pinId);  // 标记颜色随分类走
    markDirty();
    refreshProps();
}

void MainWindow::onBatchSide(const QString& side)
{
    if (!m_project)
        return;
    for (int id : m_scene->selectedPinIds()) {
        if (Pin* pin = m_project->findPin(id)) {
            pin->side = side;
            m_scene->relayoutPin(id);
        }
    }
    markDirty();
}

void MainWindow::onBatchCategory(const QString& categoryId)
{
    if (!m_project)
        return;
    bool changed = false;
    for (int id : m_scene->selectedPinIds()) {
        Pin* pin = m_project->findPin(id);
        if (!pin || pin->category == categoryId)
            continue;
        pin->category = categoryId;
        m_scene->refreshPinStyle(id);  // 定向刷新标记，避免全场景重建
        m_scene->relayoutPin(id);
        changed = true;
    }
    if (changed)
        markDirty();
}

void MainWindow::onDeleteSelection()
{
    // 焦点在文本输入框时 Del 是编辑操作，不删引脚
    if (QWidget* focus = QApplication::focusWidget()) {
        if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus)
            || qobject_cast<QComboBox*>(focus) || qobject_cast<QTextEdit*>(focus))
            return;
    }
    const QList<int> ids = m_scene->selectedPinIds();
    if (ids.isEmpty())
        return;
    m_scene->deletePins(ids);
    refreshProps();
}

// ---- 库管理 / 帮助 ----

void MainWindow::onLibraryManager()
{
    if (!m_project)
        return;
    LibraryDialog dlg(m_project.get(), this);
    connect(&dlg, &LibraryDialog::changed, this, [this]() {
        m_scene->rebuildAll();  // 分类颜色/库内容都可能变化，全量刷新
        refreshProps();
        markDirty();
    });
    dlg.exec();
}

void MainWindow::onHelp()
{
    QMessageBox::information(
        this, tr("使用说明"),
        tr("<h3>工作流</h3>"
           "<ol>"
           "<li><b>新建工程</b>：导入开发板图片 → 保存 .pinout.json，进入工程后板图居中。</li>"
           "<li><b>放置引脚</b>：工具 1-4。单pin工具：十字虚线中心 = 引脚圆心/方标记中心，单击落针；"
           "多pin工具：单击定锚点 → 向上/下移动决定数量（等距）→ Ctrl+滚轮调间距 → 再次单击确认 → 批量命名。</li>"
           "<li><b>追加复用定义</b>：工具 5。单击引脚弹出输入框，输入或从复用库选择定义后 Enter 追加；"
           "每次输入过的定义自动进入本工程复用库，下次直接下拉选择；也可框选多个引脚后在右侧属性面板批量追加。</li>"
           "<li><b>导出</b>：Ctrl+E 导出高清 PNG（板图居中 + 芯片链 + 底部图例）。</li>"
           "</ol>"
           "<h3>快捷键</h3>"
           "<p>V/1/2/3/4/5 切换工具；W 适应窗口；Ctrl+0 100%；Del 删除选中；Ctrl+S 保存；"
           "Esc 取消当前操作/返回选择；滚轮缩放；中键或空格+左键平移。</p>"));
}

}  // namespace pinout

#include "mainwindow.moc"
