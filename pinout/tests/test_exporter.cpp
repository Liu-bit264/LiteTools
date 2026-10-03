// 海报导出单测：渲染冒烟、PNG 魔数、覆盖备份、参数卫兵。
// 运行：ctest --test-dir build（offscreen）或直接运行 test_exporter。

#include "exporter.h"

#include <QTemporaryDir>
#include <QtTest>

namespace pinout {
namespace {
const struct OffscreenInit {
    OffscreenInit() { qputenv("QT_QPA_PLATFORM", "offscreen"); }
} s_offscreenInit;

Project makeProject(const QString& boardPath)
{
    Project p;
    p.filePath = boardPath + QStringLiteral(".proj.pinout.json");  // 仅占位，不参与导出
    p.imagePath = boardPath;
    p.imageSize = QSize(64, 48);
    p.categories = Project::defaultCategories();
    p.categoryOrder = Project::defaultCategoryOrder();
    Pin pin;
    pin.id = 1;
    pin.kind = QLatin1String(kKindPin);
    pin.pos = QPointF(12, 24);
    pin.name = QStringLiteral("PB12");
    pin.side = QStringLiteral("left");
    pin.category = QStringLiteral("pb");
    pin.defs = QStringList{QStringLiteral("FT")};
    p.pins.append(pin);
    p.upsertLibrary(QStringLiteral("FT"), QStringLiteral("tol5v"));
    return p;
}
}  // namespace

class ExporterTest : public QObject {
    Q_OBJECT

private slots:
    void renderSmokeAndExport();
    void exportBackupOnOverwrite();
    void sizeMismatchFails();
    void scaleGuard();
};

void ExporterTest::renderSmokeAndExport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString boardPath = dir.filePath(QStringLiteral("board.png"));
    QImage board(64, 48, QImage::Format_ARGB32);
    board.fill(0xFF3366CC);
    QVERIFY(board.save(boardPath, "PNG"));

    Project p = makeProject(boardPath);
    QString err;
    const QImage poster = Exporter::renderPoster(p, QPixmap(boardPath),
                                                 Exporter::kDefaultScale, &err);
    QVERIFY2(!poster.isNull(), qPrintable(err));
    QVERIFY(poster.width() > 64 * 2);   // 芯片链与边距使海报宽于板图
    QVERIFY(poster.height() > 48 * 2);  // 图例使海报高于板图

    const QString outPath = dir.filePath(QStringLiteral("poster.png"));
    QVERIFY2(Exporter::exportPng(p, outPath, Exporter::kDefaultScale, &err),
             qPrintable(err));
    QFile out(outPath);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray magic = out.read(8);
    QVERIFY(magic.startsWith("\x89PNG"));
    out.close();
}

void ExporterTest::exportBackupOnOverwrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString boardPath = dir.filePath(QStringLiteral("board.png"));
    QImage board(64, 48, QImage::Format_ARGB32);
    board.fill(0xFF3366CC);
    QVERIFY(board.save(boardPath, "PNG"));

    Project p = makeProject(boardPath);
    const QString outPath = dir.filePath(QStringLiteral("poster.png"));
    QString err;
    QVERIFY2(Exporter::exportPng(p, outPath, 1.0, &err), qPrintable(err));
    QVERIFY2(Exporter::exportPng(p, outPath, 1.0, &err), qPrintable(err));
    const QStringList baks = QDir(dir.path())
                                 .entryList({QStringLiteral("*.bak-") + QStringLiteral("*")},
                                            QDir::Files);
    QCOMPARE(baks.size(), 1);
    QVERIFY(baks[0].startsWith(QStringLiteral("poster.png.bak-")));
}

void ExporterTest::sizeMismatchFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString boardPath = dir.filePath(QStringLiteral("board.png"));
    QImage board(64, 48, QImage::Format_ARGB32);
    board.fill(0xFF3366CC);
    QVERIFY(board.save(boardPath, "PNG"));

    Project p = makeProject(boardPath);
    p.imageSize = QSize(100, 100);  // 与板图不符
    QString err;
    QVERIFY(Exporter::renderPoster(p, QPixmap(boardPath), 1.0, &err).isNull());
    QVERIFY(!err.isEmpty());
}

void ExporterTest::scaleGuard()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString boardPath = dir.filePath(QStringLiteral("board.png"));
    QImage board(64, 48, QImage::Format_ARGB32);
    board.fill(0xFF3366CC);
    QVERIFY(board.save(boardPath, "PNG"));

    Project p = makeProject(boardPath);
    QString err;
    QVERIFY(Exporter::renderPoster(p, QPixmap(boardPath), 0.0, &err).isNull());
    QVERIFY(!err.isEmpty());
    QVERIFY(Exporter::renderPoster(p, QPixmap(boardPath), 9.0, &err).isNull());
    QVERIFY(!err.isEmpty());
    QVERIFY(Exporter::renderPoster(p, QPixmap(), 1.0, &err).isNull());  // 空板图
    QVERIFY(!err.isEmpty());
}

}  // namespace pinout

QTEST_MAIN(pinout::ExporterTest)
#include "test_exporter.moc"
