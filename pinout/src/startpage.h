#pragma once
// 起始页：新建工程（导入开发板图片 → 保存 .pinout.json）/ 打开工程 / 最近工程列表。
// 最近列表存于 ~/.litetools/pinout_recent.json，最多 10 条。

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QString>

namespace pinout {

class StartPage : public QDialog {
    Q_OBJECT
public:
    explicit StartPage(QWidget* parent = nullptr);

    QString selectedProject() const { return m_selected; }

    // 最近工程存取（主窗口保存/打开工程后也走这里）
    static void pushRecent(const QString& projectPath);

private slots:
    void onCreate();
    void onOpen();
    void onOpenRecent(QListWidgetItem* item);
    void onRemoveRecent();

private:
    void reloadRecent();
    static QString recentStorePath();
    static QStringList loadRecent();
    static void writeRecent(const QStringList& recent);  // 原子写最近列表

    QString m_selected;
    QListWidget* m_recentList = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_openButton = nullptr;
};

}  // namespace pinout
