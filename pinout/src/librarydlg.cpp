#include "librarydlg.h"

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace pinout {

LibraryDialog::LibraryDialog(Project* project, QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("复用定义库管理"));
    resize(460, 460);

    auto* tabs = new QTabWidget(this);

    // ---- 复用定义 ----
    m_libTable = new QTableWidget(0, 2, tabs);
    m_libTable->setHorizontalHeaderLabels({tr("定义文本"), tr("分类")});
    m_libTable->horizontalHeader()->setStretchLastSection(true);
    m_libTable->verticalHeader()->setVisible(false);
    auto* libPage = new QWidget(tabs);
    auto* libLayout = new QVBoxLayout(libPage);
    libLayout->setContentsMargins(4, 4, 4, 4);
    libLayout->addWidget(m_libTable);
    auto* libBtns = new QHBoxLayout;
    auto* addLib = new QPushButton(tr("添加"));
    auto* delLib = new QPushButton(tr("删除选中"));
    connect(addLib, &QPushButton::clicked, this, &LibraryDialog::onAddLibraryRow);
    connect(delLib, &QPushButton::clicked, this, &LibraryDialog::onRemoveLibraryRow);
    libBtns->addWidget(addLib);
    libBtns->addWidget(delLib);
    libBtns->addStretch(1);
    libLayout->addLayout(libBtns);
    tabs->addTab(libPage, tr("复用定义库"));

    // ---- 分类色板 ----
    m_catTable = new QTableWidget(0, 3, tabs);
    m_catTable->setHorizontalHeaderLabels({tr("id"), tr("名称"), tr("颜色")});
    m_catTable->horizontalHeader()->setStretchLastSection(true);
    m_catTable->verticalHeader()->setVisible(false);
    auto* catPage = new QWidget(tabs);
    auto* catLayout = new QVBoxLayout(catPage);
    catLayout->setContentsMargins(4, 4, 4, 4);
    catLayout->addWidget(m_catTable);
    auto* catBtns = new QHBoxLayout;
    auto* addCat = new QPushButton(tr("添加"));
    auto* delCat = new QPushButton(tr("删除选中"));
    connect(addCat, &QPushButton::clicked, this, &LibraryDialog::onAddCategory);
    connect(delCat, &QPushButton::clicked, this, &LibraryDialog::onRemoveCategory);
    catBtns->addWidget(addCat);
    catBtns->addWidget(delCat);
    catBtns->addStretch(1);
    catLayout->addLayout(catBtns);
    tabs->addTab(catPage, tr("分类色板"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tabs);
    auto* bottom = new QHBoxLayout;
    auto* ok = new QPushButton(tr("应用并关闭"));
    auto* cancel = new QPushButton(tr("取消"));
    connect(ok, &QPushButton::clicked, this, &LibraryDialog::apply);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addStretch(1);
    bottom->addWidget(ok);
    bottom->addWidget(cancel);
    mainLayout->addLayout(bottom);

    connect(m_catTable, &QTableWidget::cellDoubleClicked, this, &LibraryDialog::onPickColor);
    fillTables();
}

void LibraryDialog::fillTables()
{
    // 复用定义表：分类列用下拉（QComboBox 单元格在 apply 时按文本回读）
    const QStringList catIds = m_project->orderedCategoryIds();
    m_libTable->setRowCount(m_project->library.size());
    for (int r = 0; r < m_project->library.size(); ++r) {
        const LibraryEntry& e = m_project->library[r];
        auto* textItem = new QTableWidgetItem(e.text);
        textItem->setFlags(textItem->flags() | Qt::ItemIsEditable);
        m_libTable->setItem(r, 0, textItem);
        QComboBox* combo = new QComboBox;
        for (const QString& id : catIds) {
            const CategoryDef& c = m_project->categories[id];
            combo->addItem(c.label, id);
        }
        combo->setCurrentIndex(combo->findData(e.category));
        m_libTable->setCellWidget(r, 1, combo);
    }

    // 分类表：id 只读，颜色单元格显示色样（双击换色）
    m_catTable->setRowCount(m_project->categories.size());
    int r = 0;
    for (const QString& id : m_project->orderedCategoryIds()) {
        const CategoryDef& c = m_project->categories[id];
        auto* idItem = new QTableWidgetItem(id);
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_catTable->setItem(r, 0, idItem);
        m_catTable->setItem(r, 1, new QTableWidgetItem(c.label));
        auto* colorItem = new QTableWidgetItem(c.color.name());
        colorItem->setBackground(c.color);
        colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable);
        m_catTable->setItem(r, 2, colorItem);
        ++r;
    }
}

void LibraryDialog::onAddLibraryRow()
{
    const int row = m_libTable->rowCount();
    m_libTable->insertRow(row);
    m_libTable->setItem(row, 0, new QTableWidgetItem(QString()));
    QComboBox* combo = new QComboBox;
    for (const QString& id : m_project->orderedCategoryIds())
        combo->addItem(m_project->categories[id].label, id);
    combo->setCurrentIndex(combo->findData(QStringLiteral("special")));
    m_libTable->setCellWidget(row, 1, combo);
    m_libTable->editItem(m_libTable->item(row, 0));
}

void LibraryDialog::onRemoveLibraryRow()
{
    const QList<QTableWidgetItem*> sel = m_libTable->selectedItems();
    QSet<int> rows;
    for (QTableWidgetItem* item : sel)
        rows.insert(item->row());
    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted)
        m_libTable->removeRow(row);
}

void LibraryDialog::onAddCategory()
{
    const int row = m_catTable->rowCount();
    m_catTable->insertRow(row);
    // id 去重需同时对照模型与表格现存行（删行后 rowCount 回退会产生重复 id）
    QString id = QStringLiteral("custom%1").arg(row);
    auto exists = [this](const QString& candidate) {
        if (m_project->categories.contains(candidate))
            return true;
        for (int r = 0; r < m_catTable->rowCount() - 1; ++r) {
            if (m_catTable->item(r, 0) && m_catTable->item(r, 0)->text() == candidate)
                return true;
        }
        return false;
    };
    while (exists(id))
        id += QLatin1Char('x');
    m_catTable->setItem(row, 0, new QTableWidgetItem(id));
    m_catTable->setItem(row, 1, new QTableWidgetItem(tr("新分类")));
    QTableWidgetItem* colorItem = new QTableWidgetItem(QStringLiteral("#8E24AA"));
    colorItem->setBackground(QColor(0x8E, 0x24, 0xAA));
    m_catTable->setItem(row, 2, colorItem);
    m_catTable->editItem(m_catTable->item(row, 1));
}

void LibraryDialog::onRemoveCategory()
{
    const QList<QTableWidgetItem*> sel = m_catTable->selectedItems();
    QSet<int> rows;
    for (QTableWidgetItem* item : sel)
        rows.insert(item->row());
    for (int row : rows) {
        const QString id = m_catTable->item(row, 0)->text();
        // 被引用的分类不允许删：引脚名称分类 / 复用库条目分类
        for (const Pin& p : m_project->pins)
            if (p.category == id) {
                QMessageBox::warning(this, tr("无法删除"),
                                     tr("分类 %1 正被引脚使用，不能删除").arg(id));
                return;
            }
        for (const LibraryEntry& e : m_project->library)
            if (e.category == id) {
                QMessageBox::warning(this, tr("无法删除"),
                                     tr("分类 %1 正被复用库条目使用，不能删除").arg(id));
                return;
            }
    }
    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted)
        m_catTable->removeRow(row);
}

void LibraryDialog::onPickColor(int row, int column)
{
    if (column != 2)
        return;
    const QColor current(m_catTable->item(row, 2)->text());
    const QColor picked = QColorDialog::getColor(current, this, tr("选择分类颜色"));
    if (!picked.isValid())
        return;
    QTableWidgetItem* item = m_catTable->item(row, 2);
    item->setText(picked.name());
    item->setBackground(picked);
}

void LibraryDialog::apply()
{
    // 写回分类（id 行文本也允许改，前提是无引用冲突——引用检查在删除时做过，改名保守处理）
    QMap<QString, CategoryDef> cats;
    for (int r = 0; r < m_catTable->rowCount(); ++r) {
        const QString id = m_catTable->item(r, 0)->text().trimmed();
        if (id.isEmpty())
            continue;
        CategoryDef c;
        c.id = id;
        c.label = m_catTable->item(r, 1)->text().isEmpty()
                      ? id
                      : m_catTable->item(r, 1)->text();
        c.color = QColor(m_catTable->item(r, 2)->text());
        cats.insert(id, c);
    }
    // 写回复用定义库（去重，文本为键）
    QList<LibraryEntry> lib;
    QSet<QString> seen;
    for (int r = 0; r < m_libTable->rowCount(); ++r) {
        const QString text = m_libTable->item(r, 0)->text().trimmed();
        if (text.isEmpty() || seen.contains(text))
            continue;
        auto* combo = qobject_cast<QComboBox*>(m_libTable->cellWidget(r, 1));
        LibraryEntry e;
        e.text = text;
        e.category = combo ? combo->currentData().toString() : QStringLiteral("special");
        lib.append(e);
        seen.insert(text);
    }

    m_project->categories = cats;
    // 表格行序即图例展示顺序（QMap.keys() 是字母序，会丢行序）
    QStringList order;
    for (int r = 0; r < m_catTable->rowCount(); ++r) {
        const QString id = m_catTable->item(r, 0)->text().trimmed();
        if (!id.isEmpty() && cats.contains(id) && !order.contains(id))
            order.append(id);
    }
    m_project->categoryOrder = order;
    m_project->library = lib;
    emit changed();
    accept();
}

}  // namespace pinout
