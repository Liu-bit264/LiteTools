// 布局几何单测：芯片链朝向/占位/虚线段、图例两行、海报包围盒。
// 运行：ctest --test-dir build（offscreen）或直接运行 test_layout。

#include "chiprender.h"
#include "layout.h"

#include <QtTest>

namespace pinout {
namespace {
const struct OffscreenInit {
    OffscreenInit() { qputenv("QT_QPA_PLATFORM", "offscreen"); }
} s_offscreenInit;

Project makeProject()
{
    Project p;
    p.imageSize = QSize(400, 300);
    p.categories = Project::defaultCategories();
    p.categoryOrder = Project::defaultCategoryOrder();
    p.upsertLibrary(QStringLiteral("FT"), QStringLiteral("tol5v"));
    p.upsertLibrary(QStringLiteral("USART1_TX"), QStringLiteral("uart"));
    return p;
}

QFontMetricsF metrics()
{
    return QFontMetricsF(chipFont());
}
}  // namespace

class LayoutTest : public QObject {
    Q_OBJECT

private slots:
    void placeholderWhenNoDefs();
    void noPlaceholderWithDefs();
    void leftSideExtendsLeftward();
    void rightSideExtendsRightward();
    void leadersMatchChips();
    void chainBoundsCoverChips();
    void legendTwoRows();
    void posterBoundsContainment();
    void defCategoryLookup();
};

Pin makePin(const QString& side, const QStringList& defs)
{
    Pin pin;
    pin.id = 1;
    pin.kind = QLatin1String(kKindPin);
    pin.pos = QPointF(20, 100);
    pin.name = QStringLiteral("PB12");
    pin.side = side;
    pin.category = QStringLiteral("pb");
    pin.defs = defs;
    return pin;
}

void LayoutTest::placeholderWhenNoDefs()
{
    const Project p = makeProject();
    const ChainLayout chain = layoutChain(p, makePin(QStringLiteral("left"), {}),
                                          metrics(), QSizeF(p.imageSize));
    QCOMPARE(chain.chips.size(), 2);  // 名称 + "-" 占位
    QVERIFY(chain.chips[1].placeholder);
    QCOMPARE(chain.chips[1].text, QStringLiteral("-"));
}

void LayoutTest::noPlaceholderWithDefs()
{
    const Project p = makeProject();
    const ChainLayout chain = layoutChain(p, makePin(QStringLiteral("left"),
                                                     {QStringLiteral("FT")}),
                                          metrics(), QSizeF(p.imageSize));
    QCOMPARE(chain.chips.size(), 2);  // 名称 + 定义，无占位
    QVERIFY(!chain.chips[1].placeholder);
    QCOMPARE(chain.chips[1].text, QStringLiteral("FT"));
    QCOMPARE(chain.chips[1].color, p.categories[QStringLiteral("tol5v")].color);
}

void LayoutTest::leftSideExtendsLeftward()
{
    const Project p = makeProject();
    const ChainLayout chain = layoutChain(p, makePin(QStringLiteral("left"),
                                                     {QStringLiteral("FT"),
                                                      QStringLiteral("USART1_TX")}),
                                          metrics(), QSizeF(p.imageSize));
    QCOMPARE(chain.chips.size(), 3);
    // 名称芯片紧贴板图左缘外 kBoardGap
    QCOMPARE(chain.chips[0].rect.right(), -kBoardGap);
    // 逐枚向左外延
    QVERIFY(chain.chips[1].rect.right() < chain.chips[0].rect.left());
    QVERIFY(chain.chips[2].rect.right() < chain.chips[1].rect.left());
    // 名称芯片垂直居中于引脚
    QCOMPARE(chain.chips[0].rect.center().y(), 100.0);
}

void LayoutTest::rightSideExtendsRightward()
{
    const Project p = makeProject();
    const ChainLayout chain = layoutChain(p, makePin(QStringLiteral("right"),
                                                     {QStringLiteral("FT")}),
                                          metrics(), QSizeF(p.imageSize));
    QCOMPARE(chain.chips[0].rect.left(), p.imageSize.width() + kBoardGap);
    QVERIFY(chain.chips[1].rect.left() > chain.chips[0].rect.right());
}

void LayoutTest::leadersMatchChips()
{
    const Project p = makeProject();
    const ChainLayout chain = layoutChain(p, makePin(QStringLiteral("right"),
                                                     {QStringLiteral("FT"),
                                                      QStringLiteral("USART1_TX")}),
                                          metrics(), QSizeF(p.imageSize));
    QCOMPARE(chain.leaders.size(), chain.chips.size());  // 引脚→首枚 + 枚间连接
    for (const QLineF& l : chain.leaders)
        QCOMPARE(l.y1(), l.y2());  // 全部水平
}

void LayoutTest::chainBoundsCoverChips()
{
    const Project p = makeProject();
    const ChainLayout chain = layoutChain(p, makePin(QStringLiteral("left"),
                                                     {QStringLiteral("FT")}),
                                          metrics(), QSizeF(p.imageSize));
    for (const Chip& c : chain.chips)
        QVERIFY2(chain.bounds.contains(c.rect), qPrintable(c.text));
    QVERIFY(chain.bounds.contains(QPointF(20, 100)));  // 引脚点也在界内
}

void LayoutTest::legendTwoRows()
{
    const Project p = makeProject();
    const LegendLayout legend = layoutLegend(p, metrics(), QRectF(0, 0, 400, 300));
    QCOMPARE(legend.rows.size(), 2);
    QCOMPARE(legend.rows[0].size() + legend.rows[1].size(), 19);
    QCOMPARE(legend.swatches.size(), 2);
    for (int r = 0; r < 2; ++r) {
        QCOMPARE(legend.rows[r].size(), legend.swatches[r].size());
        for (int i = 0; i < legend.rows[r].size(); ++i)
            QVERIFY(legend.rows[r][i].second.contains(legend.swatches[r][i]));
    }
    QVERIFY(legend.bounds.top() >= 300.0);  // 图例在板图下方
}

void LayoutTest::posterBoundsContainment()
{
    Project p = makeProject();
    Pin right;
    right.id = 1;
    right.kind = QLatin1String(kKindPin);
    right.pos = QPointF(380, 150);
    right.name = QStringLiteral("PA8");
    right.side = QStringLiteral("right");
    right.category = QStringLiteral("pa");
    right.defs = QStringList{QStringLiteral("USART1_TX")};
    p.pins.append(makePin(QStringLiteral("left"), {}));
    p.pins.append(right);

    const QRectF bounds = posterBounds(p, metrics(), QSizeF(p.imageSize));
    QVERIFY(bounds.contains(QRectF(0, 0, 400, 300)));
    for (const Pin& pin : p.pins) {
        const ChainLayout chain = layoutChain(p, pin, metrics(), QSizeF(p.imageSize));
        QVERIFY(bounds.contains(chain.bounds));
    }
    // 外扩边距生效
    QVERIFY(bounds.left() <= -kPosterMargin);
    QVERIFY(bounds.width() > 400 + 2 * kPosterMargin);
}

void LayoutTest::defCategoryLookup()
{
    const Project p = makeProject();
    QCOMPARE(defCategory(p, QStringLiteral("FT")), QStringLiteral("tol5v"));
    QCOMPARE(defCategory(p, QStringLiteral("USART1_TX")), QStringLiteral("uart"));
    QCOMPARE(defCategory(p, QStringLiteral("未知定义")), QStringLiteral("special"));
}

}  // namespace pinout

QTEST_MAIN(pinout::LayoutTest)
#include "test_layout.moc"
