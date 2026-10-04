#pragma once
// 复用定义库 / 分类色板管理对话框：表格化增删改，确定后整体写回工程。

#include "project.h"

#include <QDialog>
#include <QTableWidget>

namespace pinout {

class LibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit LibraryDialog(Project* project, QWidget* parent = nullptr);

signals:
    void changed();  // 已写回工程（主窗口负责刷新面板/场景并标脏）

private slots:
    void onAddLibraryRow();
    void onRemoveLibraryRow();
    void onAddCategory();
    void onRemoveCategory();
    void onPickColor(int row, int column);
    void apply();

private:
    void fillTables();

    Project* m_project;  // 非拥有
    QTableWidget* m_libTable = nullptr;   // 列：定义文本 | 分类
    QTableWidget* m_catTable = nullptr;   // 列：id | 名称 | 颜色
};

}  // namespace pinout
