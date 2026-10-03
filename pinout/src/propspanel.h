#pragma once
// 右侧属性面板：单选引脚的名称/侧别/类别/定义编辑；多选时的批量侧别/类别/追加定义。
// 面板零模型写入，全部通过信号交主窗口落库。

#include "project.h"

#include <QComboBox>
#include <QCompleter>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringListModel>
#include <QWidget>

namespace pinout {

class PropsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropsPanel(QWidget* parent = nullptr);
    void setProject(Project* project);  // 非拥有；分类下拉与补全随工程刷新
    void setSelection(const QList<int>& ids);
    void refreshAll();  // 分类/库/选中内容变化后重建显示
    void focusName();   // 新落针后聚焦名称输入框

signals:
    void nameEdited(int pinId, const QString& name);
    void batchSideChanged(const QString& side);
    void batchCategoryChanged(const QString& categoryId);
    void defsAppendRequested(const QString& text, const QString& categoryId);  // 应用于全部选中
    void defRemoveRequested(int pinId, const QString& text);                   // 仅单选时可用
    void libraryManagerRequested();

private slots:
    void onNameEditingFinished();
    void onSideChanged(int index);
    void onCategoryChanged(int index);
    void onAppendClicked();
    void onRemoveDefClicked();

private:
    void rebuildStaticCombos();
    void fillFromSelection();
    const Pin* singleSelected() const;

    Project* m_project = nullptr;
    QList<int> m_ids;
    bool m_updating = false;  // 程序性填值时抑制信号回灌
    QCompleter* m_defCompleter = nullptr;  // 复用库补全（refreshAll 只换 model）

    QLineEdit* m_name = nullptr;
    QComboBox* m_side = nullptr;
    QComboBox* m_category = nullptr;
    QListWidget* m_defs = nullptr;
    QComboBox* m_defText = nullptr;   // 复用库过滤补全（可输入新文本）
    QComboBox* m_defCategory = nullptr;
    QLabel* m_selInfo = nullptr;
};

}  // namespace pinout
