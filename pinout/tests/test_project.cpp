// 工程模型单测：默认分类/猜色、JSON 往返、备份、异常输入拒绝、复用库去重。
// 运行：ctest --test-dir build（offscreen）或直接运行 test_project 可执行文件。

#include "project.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>
#include <memory>

namespace pinout {
namespace {
const struct OffscreenInit {
    OffscreenInit() { qputenv("QT_QPA_PLATFORM", "offscreen"); }
} s_offscreenInit;
}  // namespace

class ProjectTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsAndGuess();
    void roundTrip();
    void backupOnSave();
    void loadRejectsGarbage();
    void loadRejectsWrongTool();
    void libraryDedup();
    void relativeImagePath();
    void invalidCategoryReference();

private:
    static std::shared_ptr<Project> makeSampleProject(const QString& filePath);
};

std::shared_ptr<Project> ProjectTest::makeSampleProject(const QString& filePath)
{
    auto p = std::make_shared<Project>();
    p->filePath = filePath;
    p->imagePath = QStringLiteral("board.png");
    p->imageSize = QSize(400, 300);
    p->categories = Project::defaultCategories();
    p->categoryOrder = Project::defaultCategoryOrder();
    p->spacing = 30.0;
    p->pinSize = 8.0;

    Pin a;
    a.id = 1;
    a.kind = QLatin1String(kKindPin);
    a.pos = QPointF(30, 60);
    a.name = QStringLiteral("PB12");
    a.side = QStringLiteral("left");
    a.category = QStringLiteral("pb");
    a.defs = QStringList{QStringLiteral("FT"), QStringLiteral("CANRX")};
    p->pins.append(a);

    Pin b;
    b.id = 2;
    b.kind = QLatin1String(kKindHeader);
    b.pos = QPointF(360, 60);
    b.name = QStringLiteral("+3.3V");
    b.side = QStringLiteral("right");
    b.category = QStringLiteral("p3v3");
    p->pins.append(b);

    p->upsertLibrary(QStringLiteral("FT"), QStringLiteral("tol5v"));
    p->upsertLibrary(QStringLiteral("CANRX"), QStringLiteral("can"));
    return p;
}

void ProjectTest::defaultsAndGuess()
{
    const auto cats = Project::defaultCategories();
    QCOMPARE(cats.size(), 19);
    QCOMPARE(Project::defaultCategoryOrder().size(), 19);
    QCOMPARE(Project::guessCategory(QStringLiteral("PA0")), QStringLiteral("pa"));
    QCOMPARE(Project::guessCategory(QStringLiteral("pb13")), QStringLiteral("pb"));
    QCOMPARE(Project::guessCategory(QStringLiteral("PC15")), QStringLiteral("pc"));
    QCOMPARE(Project::guessCategory(QStringLiteral("GND")), QStringLiteral("gnd"));
    QCOMPARE(Project::guessCategory(QStringLiteral("VSS")), QStringLiteral("gnd"));
    QCOMPARE(Project::guessCategory(QStringLiteral("+3.3V")), QStringLiteral("p3v3"));
    QCOMPARE(Project::guessCategory(QStringLiteral("VDD")), QStringLiteral("p3v3"));
    QCOMPARE(Project::guessCategory(QStringLiteral("+5V")), QStringLiteral("p5v"));
    QCOMPARE(Project::guessCategory(QStringLiteral("NRST")), QStringLiteral("special"));
    QCOMPARE(Project::guessSide(50, 400), QStringLiteral("left"));
    QCOMPARE(Project::guessSide(350, 400), QStringLiteral("right"));
    // 顺序表中不应混入未定义的 id
    for (const QString& id : Project::defaultCategoryOrder())
        QVERIFY(cats.contains(id));
    // orderedCategoryIds 覆盖全部分类
    Project p;
    p.categories = cats;
    p.categoryOrder = Project::defaultCategoryOrder();
    p.categories.insert(QStringLiteral("extra"), CategoryDef{QStringLiteral("extra"),
                                                             QStringLiteral("附加"),
                                                             QColor(0x12, 0x34, 0x56)});
    const QStringList ordered = p.orderedCategoryIds();
    QCOMPARE(ordered.size(), 20);
    QCOMPARE(ordered.last(), QStringLiteral("extra"));  // 未排序的自定义分类补尾
}

void ProjectTest::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("t.pinout.json"));

    auto p = makeSampleProject(path);
    QString err;
    QVERIFY2(p->save(&err), qPrintable(err));

    auto loaded = Project::load(path, &err);
    QVERIFY2(loaded, qPrintable(err));
    QCOMPARE(loaded->imagePath, p->imagePath);
    QCOMPARE(loaded->imageSize, p->imageSize);
    QCOMPARE(loaded->spacing, 30.0);
    QCOMPARE(loaded->pinSize, 8.0);
    QCOMPARE(loaded->pins.size(), 2);
    QCOMPARE(loaded->pins[0].defs, p->pins[0].defs);
    QCOMPARE(loaded->pins[0].pos, p->pins[0].pos);
    QCOMPARE(loaded->pins[1].kind, QLatin1String(kKindHeader));
    QCOMPARE(loaded->pins[1].category, QStringLiteral("p3v3"));
    QCOMPARE(loaded->library.size(), 2);
    QCOMPARE(loaded->categories.size(), 19);
    QCOMPARE(loaded->categoryOrder.size(), 19);
    QCOMPARE(loaded->categories[QStringLiteral("uart")].color, QColor(0x8F, 0x8F, 0x00));
    QCOMPARE(loaded->filePath, path);
}

void ProjectTest::backupOnSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("t.pinout.json"));
    auto p = makeSampleProject(path);
    QString err;
    QVERIFY(p->save(&err));
    QVERIFY(p->save(&err));  // 第二次保存必须先备份旧文件
    const QFileInfoList entries = QDir(dir.path())
                                      .entryInfoList({QStringLiteral("*.bak-*")}, QDir::Files);
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries[0].fileName().startsWith(QStringLiteral("t.pinout.json.bak-")));
}

void ProjectTest::loadRejectsGarbage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bad.pinout.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not json {");
    f.close();
    QString err;
    auto loaded = Project::load(path, &err);
    QVERIFY(!loaded);
    QVERIFY(!err.isEmpty());
}
void ProjectTest::loadRejectsWrongTool()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("wrong.pinout.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("tool"), QStringLiteral("other")},
                                      {QStringLiteral("version"), 1}})
                .toJson());
    f.close();
    QString err;
    QVERIFY(!Project::load(path, &err));
    QVERIFY(err.contains(QStringLiteral("pinout")));
}

void ProjectTest::libraryDedup()
{
    Project p;
    p.upsertLibrary(QStringLiteral("USART1_TX"), QStringLiteral("uart"));
    p.upsertLibrary(QStringLiteral("USART1_TX"), QString());  // 重复文本：忽略
    QCOMPARE(p.library.size(), 1);
    p.upsertLibrary(QStringLiteral("USART1_TX"), QStringLiteral("spi"));  // 显式新分类：更新
    QCOMPARE(p.library.size(), 1);
    QCOMPARE(p.library[0].category, QStringLiteral("spi"));
    p.upsertLibrary(QStringLiteral("  "), QStringLiteral("uart"));  // 空白文本：忽略
    QCOMPARE(p.library.size(), 1);
    p.removeLibraryEntry(QStringLiteral("USART1_TX"));
    QCOMPARE(p.library.size(), 0);
    // 空分类兜底为 special
    p.upsertLibrary(QStringLiteral("X"), QString());
    QCOMPARE(p.library[0].category, QStringLiteral("special"));
}

void ProjectTest::relativeImagePath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("t.pinout.json"));
    auto p = makeSampleProject(path);
    p->imagePath = QDir(dir.path()).filePath(QStringLiteral("board.png"));  // 绝对路径
    QString err;
    QVERIFY2(p->save(&err), qPrintable(err));
    QCOMPARE(p->imagePath, QStringLiteral("board.png"));  // 保存后归一化为相对
    QVERIFY(QFileInfo(p->absoluteImagePath()).isAbsolute());
}

void ProjectTest::invalidCategoryReference()
{
    Project p;
    p.imagePath = QStringLiteral("board.png");
    p.imageSize = QSize(100, 100);
    p.categories = Project::defaultCategories();
    Pin pin;
    pin.id = 1;
    pin.kind = QLatin1String(kKindPin);
    pin.side = QStringLiteral("left");
    pin.category = QStringLiteral("不存在的分类");
    p.pins.append(pin);
    QString why;
    QVERIFY2(!p.isValid(&why), qPrintable(why));
    QString err;
    QVERIFY(!p.save(&err));
}

}  // namespace pinout

QTEST_MAIN(pinout::ProjectTest)
#include "test_project.moc"
