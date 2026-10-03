#pragma once
// 工程数据模型：.pinout.json 的内存表示与读写。
// 纯模型层（可离线单测，不依赖任何 GUI 头）。
// 仓规对齐：写文件 = 先备份旧文件（.bak-时间戳，同秒追加序号）再 QSaveFile 原子替换。

#include <QColor>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>

namespace pinout {

// 引脚种类：圆形单 pin / 方形排针
inline constexpr const char* kKindPin = "pin";
inline constexpr const char* kKindHeader = "header";

struct CategoryDef {
    QString id;
    QString label;
    QColor color;
};

// 复用定义库条目：曾经输入过的引脚定义文本及其分类
struct LibraryEntry {
    QString text;
    QString category;
};

struct Pin {
    int id = 0;
    QString kind;      // kKindPin / kKindHeader
    QPointF pos;       // 板图像素坐标（世界坐标）
    QString name;      // 引脚名（名称芯片内容）
    QString side;      // "left" / "right"：定义链向板图左/右侧外延
    QString category;  // 名称芯片的分类 id（端口组/电源等）
    QStringList defs;  // 追加的复用定义文本（顺序即芯片链顺序）
};

class Project {
public:
    int version = 1;
    QString imagePath;  // 相对工程文件目录的路径（load/save 时归一化）
    QSize imageSize;    // 板图像素尺寸（加载板图后回填并保存）
    QList<Pin> pins;
    QList<LibraryEntry> library;
    QMap<QString, CategoryDef> categories;
    QStringList categoryOrder;  // 图例/选择框的分类展示顺序（缺失的按 id 排序补尾）
    qreal spacing = 24.0;  // 多pin工具默认等距间距（世界像素）
    qreal pinSize = 7.0;   // 引脚标记半径/半边长（世界像素）

    QString filePath;  // 工程文件绝对路径（save 用）

    // ---- 工厂与默认值 ----
    static QMap<QString, CategoryDef> defaultCategories();
    static QStringList defaultCategoryOrder();
    // 展示顺序：categoryOrder 中存在且有效的 id 优先，其余（新增未排序的）按 id 排序补尾
    QStringList orderedCategoryIds() const;
    // 按引脚名猜测名称芯片分类：PA*/PB*/PC*/GND/VSS/+3.3V/VDD/+5V → 对应组，其余归特殊功能
    static QString guessCategory(const QString& pinName);
    static QString guessSide(qreal pinX, qreal imageWidth);

    // ---- 模型操作 ----
    Pin* findPin(int id);
    const Pin* findPin(int id) const;
    int nextPinId() const;
    void removePin(int id);
    // 复用库去重插入：文本已存在则仅在被显式给出分类时更新分类
    void upsertLibrary(const QString& text, const QString& category);
    void removeLibraryEntry(const QString& text);

    // ---- 持久化 ----
    // 结构校验：非法即返回 false 并写 why（load 前置与 GUI 保存前共用）
    bool isValid(QString* why = nullptr) const;
    static std::shared_ptr<Project> load(const QString& filePath, QString* error = nullptr);
    bool save(QString* error = nullptr);
    QString absoluteImagePath() const;

    // 备份旧文件：<name>.bak-YYYYmmdd-HHMMSS[-N]；目标不存在则静默成功
    static bool backupFile(const QString& path, QString* error = nullptr);
};

}  // namespace pinout
