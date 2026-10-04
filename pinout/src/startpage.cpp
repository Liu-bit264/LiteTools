#include "startpage.h"

#include "project.h"

#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>

namespace pinout {

namespace {
constexpr int kMaxRecent = 10;
const char* kImageFilter = "图片 (*.png *.jpg *.jpeg *.bmp *.gif);;所有文件 (*)";
const char* kProjectFilter = "pinout 工程 (*.pinout.json);;所有文件 (*)";
}  // namespace

StartPage::StartPage(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("LiteTools · pinout 引脚定义图工具"));
    setMinimumSize(460, 380);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* title = new QLabel(tr("<b>开发板引脚定义图制作工具</b>"));
    title->setStyleSheet(QStringLiteral("font-size: 16px;"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(tr("每个任务一个工程：新建时导入开发板图片，"
                                    "进入工程后图片居中显示。")));

    auto* btnRow = new QHBoxLayout;
    m_newButton = new QPushButton(tr("新建工程"));
    m_openButton = new QPushButton(tr("打开工程…"));
    m_newButton->setMinimumHeight(32);
    m_openButton->setMinimumHeight(32);
    connect(m_newButton, &QPushButton::clicked, this, &StartPage::onCreate);
    connect(m_openButton, &QPushButton::clicked, this, &StartPage::onOpen);
    btnRow->addWidget(m_newButton);
    btnRow->addWidget(m_openButton);
    layout->addLayout(btnRow);

    layout->addWidget(new QLabel(tr("最近工程")));
    m_recentList = new QListWidget;
    m_recentList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, &StartPage::onOpenRecent);
    connect(m_recentList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_recentList);
        auto* removeAct = menu.addAction(tr("从列表移除"));
        connect(removeAct, &QAction::triggered, this, &StartPage::onRemoveRecent);
        menu.exec(m_recentList->mapToGlobal(pos));
    });
    layout->addWidget(m_recentList, 1);

    reloadRecent();
}

void StartPage::onCreate()
{
    const QString image = QFileDialog::getOpenFileName(this, tr("导入开发板图片"), QString(),
                                                       QLatin1String(kImageFilter));
    if (image.isEmpty())
        return;
    QImageReader reader(image);
    const QSize size = reader.size();
    if (!reader.canRead() || size.isEmpty()) {
        QMessageBox::warning(this, tr("图片不可用"), tr("无法读取图片：%1").arg(image));
        return;
    }
    const QString suggested = QFileInfo(image).completeBaseName() + QStringLiteral(".pinout.json");
    const QString target = QFileDialog::getSaveFileName(this, tr("保存工程到"), suggested,
                                                        QLatin1String(kProjectFilter));
    if (target.isEmpty())
        return;

    Project project;
    project.filePath = QFileInfo(target).absoluteFilePath();
    project.imagePath = image;  // save 时归一化为相对路径
    project.imageSize = size;
    project.categories = Project::defaultCategories();
    project.categoryOrder = Project::defaultCategoryOrder();
    QString err;
    if (!project.save(&err)) {
        QMessageBox::warning(this, tr("创建失败"), err);
        return;
    }
    m_selected = project.filePath;
    pushRecent(m_selected);
    accept();
}

void StartPage::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("打开工程"), QString(),
                                                      QLatin1String(kProjectFilter));
    if (path.isEmpty())
        return;
    m_selected = path;
    pushRecent(m_selected);
    accept();
}

void StartPage::onOpenRecent(QListWidgetItem* item)
{
    const QString path = item->data(Qt::UserRole).toString();
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("文件不存在"), path);
        return;
    }
    m_selected = path;
    pushRecent(m_selected);
    accept();
}

void StartPage::onRemoveRecent()
{
    QListWidgetItem* item = m_recentList->currentItem();
    if (!item)
        return;
    QStringList recent = loadRecent();
    recent.removeAll(item->data(Qt::UserRole).toString());
    writeRecent(recent);
    reloadRecent();
}

void StartPage::reloadRecent()
{
    m_recentList->clear();
    for (const QString& path : loadRecent()) {
        auto* item = new QListWidgetItem(path, m_recentList);
        item->setData(Qt::UserRole, path);
        if (!QFileInfo::exists(path))
            item->setForeground(Qt::gray);
    }
}

QString StartPage::recentStorePath()
{
    return QDir::home().filePath(QStringLiteral(".litetools/pinout_recent.json"));
}

QStringList StartPage::loadRecent()
{
    QFile f(recentStorePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return {};  // 损坏/非预期内容视作无历史（下次保存即重建）
    QStringList out;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("recent")).toArray()) {
        const QString p = v.toString();
        if (!p.isEmpty() && !out.contains(p))
            out << p;
    }
    return out;
}

void StartPage::pushRecent(const QString& projectPath)
{
    QStringList recent = loadRecent();
    recent.removeAll(projectPath);
    recent.prepend(projectPath);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    writeRecent(recent);
}

void StartPage::writeRecent(const QStringList& recent)
{
    // QSaveFile 原子写；最近列表属尽力而为的便利数据，失败静默（下次打开自愈为空）
    const QString store = recentStorePath();
    QDir().mkpath(QFileInfo(store).absolutePath());
    QJsonArray arr;
    for (const QString& p : recent)
        arr.append(p);
    QJsonObject root;
    root.insert(QStringLiteral("recent"), arr);
    QSaveFile f(store);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

}  // namespace pinout
