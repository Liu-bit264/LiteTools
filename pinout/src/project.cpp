#include "project.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <algorithm>
#include <memory>

namespace pinout {

// 分类色板默认值：与目标示意图（嘉立创式引脚定义图图例）一致的近似配色；
// 随工程保存，用户可在"复用定义库管理"中改色/改名/增删。
QMap<QString, CategoryDef> Project::defaultCategories()
{
    const QList<CategoryDef> preset = {
        {QStringLiteral("uart"),    QStringLiteral("串口外设"),   QColor(0x8F, 0x8F, 0x00)},
        {QStringLiteral("spi"),     QStringLiteral("SPI外设"),    QColor(0xE9, 0x1E, 0xC4)},
        {QStringLiteral("timer"),   QStringLiteral("定时器外设"), QColor(0x26, 0xA9, 0xC4)},
        {QStringLiteral("can"),     QStringLiteral("CAN外设"),    QColor(0xC9, 0x7B, 0x84)},
        {QStringLiteral("i2c"),     QStringLiteral("I2C外设"),    QColor(0x4D, 0xB6, 0xAC)},
        {QStringLiteral("usb"),     QStringLiteral("USB外设"),    QColor(0x7E, 0x57, 0xC2)},
        {QStringLiteral("adc"),     QStringLiteral("ADC外设"),    QColor(0x2E, 0x7D, 0x32)},
        {QStringLiteral("clk"),     QStringLiteral("低速时钟引脚"), QColor(0x1B, 0x5E, 0x5A)},
        {QStringLiteral("wkup"),    QStringLiteral("唤醒引脚"),   QColor(0xA5, 0xD6, 0xA7)},
        {QStringLiteral("rtc"),     QStringLiteral("RTC外设"),    QColor(0xC0, 0xCA, 0x33)},
        {QStringLiteral("special"), QStringLiteral("特殊功能引脚"), QColor(0x8E, 0x24, 0xAA)},
        {QStringLiteral("pa"),      QStringLiteral("PA组端口"),   QColor(0x55, 0x8B, 0x2F)},
        {QStringLiteral("pb"),      QStringLiteral("PB组端口"),   QColor(0x8B, 0x8B, 0x00)},
        {QStringLiteral("pc"),      QStringLiteral("PC组端口"),   QColor(0x21, 0x96, 0xF3)},
        {QStringLiteral("p5v"),     QStringLiteral("+5V"),        QColor(0x21, 0x21, 0x21)},
        {QStringLiteral("gnd"),     QStringLiteral("GND"),        QColor(0x21, 0x21, 0x21)},
        {QStringLiteral("p3v3"),    QStringLiteral("+3.3V"),      QColor(0xE5, 0x39, 0x35)},
        {QStringLiteral("tol5v"),   QStringLiteral("5V耐压"),     QColor(0x8D, 0x6E, 0x63)},
        {QStringLiteral("tol3v3"),  QStringLiteral("3.3V耐压"),   QColor(0xF4, 0x8F, 0xB1)},
    };
    QMap<QString, CategoryDef> out;
    for (const CategoryDef& c : preset)
        out.insert(c.id, c);
    return out;
}

QStringList Project::defaultCategoryOrder()
{
    return {
        QStringLiteral("uart"),   QStringLiteral("spi"),    QStringLiteral("timer"),
        QStringLiteral("can"),    QStringLiteral("i2c"),    QStringLiteral("usb"),
        QStringLiteral("adc"),    QStringLiteral("clk"),    QStringLiteral("wkup"),
        QStringLiteral("rtc"),    QStringLiteral("special"), QStringLiteral("pa"),
        QStringLiteral("pb"),     QStringLiteral("pc"),     QStringLiteral("p5v"),
        QStringLiteral("gnd"),    QStringLiteral("p3v3"),   QStringLiteral("tol5v"),
        QStringLiteral("tol3v3"),
    };
}

QStringList Project::orderedCategoryIds() const
{
    QStringList out;
    for (const QString& id : categoryOrder)
        if (categories.contains(id) && !out.contains(id))
            out.append(id);
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it)
        if (!out.contains(it.key()))
            out.append(it.key());
    return out;
}

QString Project::guessCategory(const QString& pinName)
{
    const QString n = pinName.toUpper();
    if (n.startsWith(QStringLiteral("PA")))
        return QStringLiteral("pa");
    if (n.startsWith(QStringLiteral("PB")))
        return QStringLiteral("pb");
    if (n.startsWith(QStringLiteral("PC")))
        return QStringLiteral("pc");
    if (n.contains(QStringLiteral("GND")) || n.contains(QStringLiteral("VSS")))
        return QStringLiteral("gnd");
    if (n.contains(QStringLiteral("3.3")) || n.contains(QStringLiteral("3V3"))
        || n.contains(QStringLiteral("VDD")) || n.contains(QStringLiteral("VCC")))
        return QStringLiteral("p3v3");
    if (n.contains(QStringLiteral("+5")) || n.contains(QStringLiteral("5V")))
        return QStringLiteral("p5v");
    return QStringLiteral("special");
}

QString Project::guessSide(qreal pinX, qreal imageWidth)
{
    return pinX * 2.0 < imageWidth ? QStringLiteral("left") : QStringLiteral("right");
}

Pin* Project::findPin(int id)
{
    for (Pin& p : pins)
        if (p.id == id)
            return &p;
    return nullptr;
}

const Pin* Project::findPin(int id) const
{
    for (const Pin& p : pins)
        if (p.id == id)
            return &p;
    return nullptr;
}

int Project::nextPinId() const
{
    int maxId = 0;
    for (const Pin& p : pins)
        maxId = (std::max)(maxId, p.id);
    return maxId + 1;
}

void Project::removePin(int id)
{
    for (int i = 0; i < pins.size(); ++i) {
        if (pins[i].id == id) {
            pins.removeAt(i);
            return;
        }
    }
}

void Project::upsertLibrary(const QString& text, const QString& category)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return;
    for (LibraryEntry& e : library) {
        if (e.text == t) {
            if (!category.isEmpty())
                e.category = category;
            return;
        }
    }
    library.append(LibraryEntry{t, category.isEmpty() ? QStringLiteral("special") : category});
}

void Project::removeLibraryEntry(const QString& text)
{
    for (int i = 0; i < library.size(); ++i) {
        if (library[i].text == text) {
            library.removeAt(i);
            return;
        }
    }
}

bool Project::isValid(QString* why) const
{
    auto fail = [why](const QString& msg) {
        if (why)
            *why = msg;
        return false;
    };
    if (imagePath.isEmpty())
        return fail(QStringLiteral("board_image 不能为空"));
    if (imageSize.width() <= 0 || imageSize.height() <= 0)
        return fail(QStringLiteral("image_size 非法（须为正整数）"));
    if (spacing <= 0)
        return fail(QStringLiteral("spacing 必须为正数"));
    if (pinSize <= 0)
        return fail(QStringLiteral("pin_size 必须为正数"));
    QList<int> ids;
    for (int i = 0; i < pins.size(); ++i) {
        const Pin& p = pins[i];
        if (p.id <= 0)
            return fail(QStringLiteral("pins[%1].id 非法（必须为正整数）").arg(i));
        if (p.kind != QLatin1String(kKindPin) && p.kind != QLatin1String(kKindHeader))
            return fail(QStringLiteral("pins[%1].kind 非法：%2").arg(i).arg(p.kind));
        if (p.side != QStringLiteral("left") && p.side != QStringLiteral("right"))
            return fail(QStringLiteral("pins[%1].side 非法：%2").arg(i).arg(p.side));
        if (!categories.contains(p.category))
            return fail(QStringLiteral("pins[%1] 引用了不存在的分类：%2").arg(i).arg(p.category));
        for (const QString& d : p.defs) {
            const LibraryEntry* e = nullptr;
            for (const LibraryEntry& le : library)
                if (le.text == d) {
                    e = &le;
                    break;
                }
            // 定义文本允许不在库中（兼容手改文件），但引用的分类必须存在
            if (e && !categories.contains(e->category))
                return fail(QStringLiteral("库条目 %1 引用了不存在的分类：%2").arg(e->text, e->category));
        }
        if (ids.contains(p.id))
            return fail(QStringLiteral("pins 存在重复 id：%1").arg(p.id));
        ids.append(p.id);
    }
    return true;
}

static QJsonObject categoryToJson(const CategoryDef& c)
{
    QJsonObject o;
    o.insert(QStringLiteral("label"), c.label);
    o.insert(QStringLiteral("color"), c.color.name());
    return o;
}

static QJsonObject pinToJson(const Pin& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), p.id);
    o.insert(QStringLiteral("kind"), p.kind);
    o.insert(QStringLiteral("x"), p.pos.x());
    o.insert(QStringLiteral("y"), p.pos.y());
    o.insert(QStringLiteral("name"), p.name);
    o.insert(QStringLiteral("side"), p.side);
    o.insert(QStringLiteral("category"), p.category);
    QJsonArray defs;
    for (const QString& d : p.defs)
        defs.append(d);
    o.insert(QStringLiteral("defs"), defs);
    return o;
}

bool Project::backupFile(const QString& path, QString* error)
{
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
        return true;  // 无旧文件即无事可做
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    // 仓规对齐（ico/generator.py）：<全名>.bak-<时间戳>，同秒冲突追加序号
    QString target = path + QStringLiteral(".bak-") + stamp;
    int serial = 1;
    while (QFileInfo::exists(target)) {
        ++serial;
        target = path + QStringLiteral(".bak-") + stamp + QStringLiteral("-%1").arg(serial);
    }
    if (!QFile::copy(path, target)) {
        if (error)
            *error = QStringLiteral("备份失败：%1 → %2").arg(path, target);
        return false;
    }
    return true;
}

std::shared_ptr<Project> Project::load(const QString& filePath, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return std::shared_ptr<Project>();
    };
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("无法打开工程文件：%1").arg(filePath));
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (doc.isNull())
        return fail(QStringLiteral("工程 JSON 解析失败（偏移 %1）：%2")
                        .arg(perr.offset)
                        .arg(perr.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("工程文件顶层必须是 JSON 对象"));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("tool")).toString() != QStringLiteral("pinout"))
        return fail(QStringLiteral("tool 字段必须是 \"pinout\""));
    if (root.value(QStringLiteral("version")).toInt(0) != 1)
        return fail(QStringLiteral("不支持的工程版本（仅支持 version=1）"));

    auto sp = std::make_shared<Project>();
    sp->filePath = QFileInfo(filePath).absoluteFilePath();
    sp->version = 1;
    sp->imagePath = root.value(QStringLiteral("board_image")).toString();
    const QJsonArray sizeArr = root.value(QStringLiteral("image_size")).toArray();
    if (sizeArr.size() == 2)
        sp->imageSize = QSize(sizeArr.at(0).toInt(), sizeArr.at(1).toInt());
    sp->spacing = root.value(QStringLiteral("spacing")).toDouble(24.0);
    sp->pinSize = root.value(QStringLiteral("pin_size")).toDouble(7.0);

    const QJsonObject cats = root.value(QStringLiteral("categories")).toObject();
    for (auto it = cats.begin(); it != cats.end(); ++it) {
        const QJsonObject co = it.value().toObject();
        CategoryDef c;
        c.id = it.key();
        c.label = co.value(QStringLiteral("label")).toString(it.key());
        c.color = QColor(co.value(QStringLiteral("color")).toString(QStringLiteral("#8E24AA")));
        sp->categories.insert(c.id, c);
    }
    if (sp->categories.isEmpty()) {
        sp->categories = Project::defaultCategories();
        sp->categoryOrder = Project::defaultCategoryOrder();
    }

    QJsonArray orderArr = root.value(QStringLiteral("category_order")).toArray();
    if (orderArr.isEmpty()) {
        // 旧文件无顺序字段：默认顺序 + 追加的自定义分类
        for (const QString& id : Project::defaultCategoryOrder())
            if (sp->categories.contains(id))
                orderArr.append(id);
    }
    for (const QJsonValue& v : orderArr)
        sp->categoryOrder.append(v.toString());

    const QJsonValue pinsValue = root.value(QStringLiteral("pins"));
    if (!pinsValue.isArray() && !pinsValue.isNull())
        return fail(QStringLiteral("pins 必须是数组"));
    const QJsonArray pins = pinsValue.toArray();
    for (const QJsonValue& v : pins) {
        const QJsonObject po = v.toObject();
        Pin p;
        p.id = po.value(QStringLiteral("id")).toInt();
        if (p.id <= 0)
            return fail(QStringLiteral("pins 存在缺失或非法的 id（必须为正整数）"));
        p.kind = po.value(QStringLiteral("kind")).toString();
        p.pos = QPointF(po.value(QStringLiteral("x")).toDouble(),
                        po.value(QStringLiteral("y")).toDouble());
        p.name = po.value(QStringLiteral("name")).toString();
        p.side = po.value(QStringLiteral("side")).toString();
        p.category = po.value(QStringLiteral("category")).toString();
        for (const QJsonValue& d : po.value(QStringLiteral("defs")).toArray())
            p.defs.append(d.toString());
        sp->pins.append(p);
    }

    const QJsonValue libValue = root.value(QStringLiteral("library"));
    if (!libValue.isArray() && !libValue.isNull())
        return fail(QStringLiteral("library 必须是数组"));
    const QJsonArray lib = libValue.toArray();
    for (const QJsonValue& v : lib) {
        const QJsonObject lo = v.toObject();
        LibraryEntry e;
        e.text = lo.value(QStringLiteral("text")).toString();
        e.category = lo.value(QStringLiteral("category")).toString();
        if (!e.text.isEmpty())
            sp->library.append(e);
    }

    QString why;
    if (!sp->isValid(&why))
        return fail(why);
    return sp;
}

bool Project::save(QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (filePath.isEmpty())
        return fail(QStringLiteral("工程未指定保存路径"));
    QString why;
    if (!isValid(&why))
        return fail(why);

    // 板图路径一律存为相对工程文件的路径（跨目录可迁移）
    const QDir projDir = QFileInfo(filePath).absoluteDir();
    imagePath = projDir.relativeFilePath(QFileInfo(absoluteImagePath()).absoluteFilePath());

    QJsonObject root;
    root.insert(QStringLiteral("tool"), QStringLiteral("pinout"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("board_image"), imagePath);
    QJsonArray sizeArr;
    sizeArr.append(imageSize.width());
    sizeArr.append(imageSize.height());
    root.insert(QStringLiteral("image_size"), sizeArr);
    root.insert(QStringLiteral("spacing"), spacing);
    root.insert(QStringLiteral("pin_size"), pinSize);

    QJsonObject cats;
    for (auto it = categories.constBegin(); it != categories.constEnd(); ++it)
        cats.insert(it.key(), categoryToJson(it.value()));
    root.insert(QStringLiteral("categories"), cats);
    QJsonArray orderArr;
    for (const QString& id : orderedCategoryIds())
        orderArr.append(id);
    root.insert(QStringLiteral("category_order"), orderArr);

    QJsonArray pinsArr;
    for (const Pin& p : pins)
        pinsArr.append(pinToJson(p));
    root.insert(QStringLiteral("pins"), pinsArr);

    QJsonArray libArr;
    for (const LibraryEntry& e : library) {
        QJsonObject lo;
        lo.insert(QStringLiteral("text"), e.text);
        lo.insert(QStringLiteral("category"), e.category);
        libArr.append(lo);
    }
    root.insert(QStringLiteral("library"), libArr);

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);

    if (!backupFile(filePath, error))
        return false;

    QSaveFile f(filePath);  // 临时文件 + 原子替换
    if (!f.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("无法写入工程文件：%1").arg(filePath));
    if (f.write(payload) != payload.size())
        return fail(QStringLiteral("写入工程文件不完整：%1").arg(filePath));
    if (!f.commit())
        return fail(QStringLiteral("工程文件提交失败：%1").arg(filePath));
    return true;
}

QString Project::absoluteImagePath() const
{
    if (QDir::isAbsolutePath(imagePath))
        return imagePath;
    return QFileInfo(filePath).absoluteDir().filePath(imagePath);
}

}  // namespace pinout
