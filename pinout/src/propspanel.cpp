#include "propspanel.h"

#include "layout.h"

#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace pinout {

PropsPanel::PropsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_selInfo = new QLabel(tr("未选中引脚"));
    layout->addWidget(m_selInfo);

    auto* form = new QFormLayout;
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("如 PB12 / +3.3V"));
    connect(m_name, &QLineEdit::editingFinished, this, &PropsPanel::onNameEditingFinished);
    form->addRow(tr("名称"), m_name);

    m_side = new QComboBox;
    m_side->addItem(tr("左侧"), QStringLiteral("left"));
    m_side->addItem(tr("右侧"), QStringLiteral("right"));
    connect(m_side, &QComboBox::activated, this, &PropsPanel::onSideChanged);
    form->addRow(tr("定义链侧别"), m_side);

    m_category = new QComboBox;
    connect(m_category, &QComboBox::activated, this, &PropsPanel::onCategoryChanged);
    form->addRow(tr("名称芯片分类"), m_category);
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("复用定义（追加到选中引脚）")));
    m_defs = new QListWidget;
    m_defs->setMaximumHeight(160);
    m_defs->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* delAct = new QAction(tr("删除该定义"), m_defs);
    m_defs->addAction(delAct);
    connect(delAct, &QAction::triggered, this, &PropsPanel::onRemoveDefClicked);
    layout->addWidget(m_defs);

    m_defText = new QComboBox;
    m_defText->setEditable(true);
    m_defText->setInsertPolicy(QComboBox::NoInsert);
    m_defText->lineEdit()->setPlaceholderText(tr("输入或从复用库选择"));
    // 复用同一个 QCompleter（refreshAll 只更新其 model，避免每次刷新泄漏旧实例）
    m_defCompleter = new QCompleter(m_defText);
    m_defCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_defCompleter->setFilterMode(Qt::MatchContains);
    m_defText->setCompleter(m_defCompleter);
    connect(m_defText->lineEdit(), &QLineEdit::textEdited, this, [this](const QString& text) {
        // 复用联动：命中库条目时自动切到其分类（连接一次，避免 rebuild 重复挂接）
        if (!m_project)
            return;
        for (const LibraryEntry& e : m_project->library) {
            if (e.text.compare(text, Qt::CaseInsensitive) == 0) {
                const int idx = m_defCategory->findData(e.category);
                if (idx >= 0)
                    m_defCategory->setCurrentIndex(idx);
                return;
            }
        }
    });
    m_defCategory = new QComboBox;
    auto* appendRow = new QHBoxLayout;
    appendRow->addWidget(m_defText, 3);
    appendRow->addWidget(m_defCategory, 2);
    layout->addLayout(appendRow);

    auto* appendBtn = new QPushButton(tr("追加定义（Enter）"));
    connect(appendBtn, &QPushButton::clicked, this, &PropsPanel::onAppendClicked);
    layout->addWidget(appendBtn);

    auto* libBtn = new QPushButton(tr("复用定义库管理…"));
    connect(libBtn, &QPushButton::clicked, this, &PropsPanel::libraryManagerRequested);
    layout->addWidget(libBtn);
    layout->addStretch(1);
}

void PropsPanel::setProject(Project* project)
{
    m_project = project;
    rebuildStaticCombos();
    setSelection({});
}

void PropsPanel::rebuildStaticCombos()
{
    m_updating = true;
    // 名称芯片分类下拉
    m_category->clear();
    m_defCategory->clear();
    if (m_project) {
        for (const QString& id : m_project->orderedCategoryIds()) {
            const CategoryDef& c = m_project->categories[id];
            QPixmap swatch(12, 12);
            swatch.fill(c.color);
            m_category->addItem(QIcon(swatch), c.label, id);
            m_defCategory->addItem(QIcon(swatch), c.label, id);
        }
        // 复用库文本补全
        QStringList texts;
        for (const LibraryEntry& e : m_project->library)
            texts << e.text;
        auto* model = qobject_cast<QStringListModel*>(m_defCompleter->model());
        if (!model) {
            model = new QStringListModel(m_defCompleter);
            m_defCompleter->setModel(model);
        }
        model->setStringList(texts);
    }
    m_updating = false;
}

void PropsPanel::setSelection(const QList<int>& ids)
{
    m_ids = ids;
    fillFromSelection();
}

const Pin* PropsPanel::singleSelected() const
{
    if (!m_project || m_ids.size() != 1)
        return nullptr;
    return m_project->findPin(m_ids.first());
}

void PropsPanel::fillFromSelection()
{
    m_updating = true;
    if (m_ids.isEmpty()) {
        m_selInfo->setText(tr("未选中引脚"));
        m_name->clear();
        m_defs->clear();
        m_name->setEnabled(false);
        m_side->setEnabled(false);
        m_category->setEnabled(false);
        m_defs->setEnabled(false);
        m_defText->setEnabled(false);
        m_defCategory->setEnabled(false);
        m_updating = false;
        return;
    }
    const bool multi = m_ids.size() > 1;
    m_selInfo->setText(multi ? tr("已选中 %1 个引脚（批量模式）").arg(m_ids.size())
                             : tr("引脚 #%1").arg(m_ids.first()));
    const Pin* pin = m_project ? m_project->findPin(m_ids.first()) : nullptr;
    m_name->setEnabled(!multi);
    m_side->setEnabled(true);
    m_category->setEnabled(true);
    m_name->setText(multi ? QString() : (pin ? pin->name : QString()));
    if (pin) {
        m_side->setCurrentIndex(m_side->findData(pin->side));
        const int catIdx = m_category->findData(pin->category);
        m_category->setCurrentIndex(catIdx);
    }
    m_defs->setEnabled(!multi);
    m_defs->clear();
    if (!multi && pin) {
        for (const QString& def : pin->defs) {
            QListWidgetItem* item = new QListWidgetItem(def, m_defs);
            const QString cat = defCategory(*m_project, def);
            const auto it = m_project->categories.constFind(cat);
            QPixmap swatch(12, 12);
            swatch.fill(it == m_project->categories.constEnd() ? QColor(0x90, 0x90, 0x90)
                                                               : it->color);
            item->setIcon(QIcon(swatch));
        }
    }
    m_defText->setEnabled(true);
    m_defCategory->setEnabled(true);
    m_updating = false;
}

void PropsPanel::focusName()
{
    m_name->setFocus();
    m_name->selectAll();
}

void PropsPanel::refreshAll()
{
    rebuildStaticCombos();
    fillFromSelection();
}

void PropsPanel::onNameEditingFinished()
{
    if (m_updating || m_ids.size() != 1 || !m_project)
        return;
    const Pin* pin = singleSelected();
    if (!pin || pin->name == m_name->text().trimmed())
        return;
    emit nameEdited(m_ids.first(), m_name->text().trimmed());
}

void PropsPanel::onSideChanged(int)
{
    if (m_updating || m_ids.isEmpty())
        return;
    emit batchSideChanged(m_side->currentData().toString());
}

void PropsPanel::onCategoryChanged(int)
{
    if (m_updating || m_ids.isEmpty())
        return;
    emit batchCategoryChanged(m_category->currentData().toString());
}

void PropsPanel::onAppendClicked()
{
    if (m_ids.isEmpty())
        return;
    const QString text = m_defText->currentText().trimmed();
    if (text.isEmpty())
        return;
    const QString cat = m_defCategory->currentData().toString();
    emit defsAppendRequested(text, cat);
    m_defText->setCurrentText(QString());
}

void PropsPanel::onRemoveDefClicked()
{
    const Pin* pin = singleSelected();
    QListWidgetItem* item = m_defs->currentItem();
    if (!pin || !item)
        return;
    emit defRemoveRequested(pin->id, item->text());
}

}  // namespace pinout
