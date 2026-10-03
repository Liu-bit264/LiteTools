#pragma once
// 布局纯几何：给定工程与字体度量，计算名称/定义芯片链与图例的世界坐标矩形。
// 画布（QGraphicsScene）与导出（QPainter→QImage）共用，保证所见即所得。

#include "project.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QRectF>
#include <QSizeF>
#include <QVector>
#include <utility>

namespace pinout {

// ---- 世界坐标常量（画布与导出一致）----
inline constexpr qreal kChipHeight = 18.0;
inline constexpr qreal kChipPadX = 5.0;
inline constexpr qreal kChipGap = 12.0;    // 相邻芯片之间的虚线段长度
inline constexpr qreal kBoardGap = 6.0;    // 名称芯片与板图边缘的间距
inline constexpr qreal kMinChipWidth = 22.0;
inline constexpr qreal kLegendSwatch = 12.0;
inline constexpr qreal kLegendPadX = 6.0;
inline constexpr qreal kLegendGap = 14.0;  // 图例条目间距
inline constexpr qreal kLegendRowGap = 10.0;
inline constexpr qreal kLegendTopGap = 18.0;
inline constexpr qreal kPosterMargin = 24.0;

struct Chip {
    QString text;
    QColor color;
    bool placeholder = false;  // 无定义时的 "-" 占位芯片
    QRectF rect;               // 世界坐标
};

struct ChainLayout {
    QVector<Chip> chips;      // chips[0]=名称芯片；其后为定义芯片（无定义时恰为 1 个 "-" 占位）
    QVector<QLineF> leaders;  // 虚线段：引脚→首芯片 + 芯片间连接（size == chips.size()）
    QRectF bounds;
};

struct LegendLayout {
    QVector<QVector<std::pair<CategoryDef, QRectF>>> rows;  // 每行：分类 + 整块矩形（色块+文字）
    QVector<QVector<QRectF>> swatches;                      // 与 rows 平行：纯色块子矩形
    QRectF bounds;
};

ChainLayout layoutChain(const Project& project, const Pin& pin,
                        const QFontMetricsF& fm, const QSizeF& imageSize);

// 图例铺在板图正下方：分类较多时拆两行（上限两行，超出按列均分压缩间距）
LegendLayout layoutLegend(const Project& project, const QFontMetricsF& fm,
                          const QRectF& imageRect);

// 海报整体包围盒 = 板图 ∪ 全部芯片链 ∪ 图例，再外扩 kPosterMargin
QRectF posterBounds(const Project& project, const QFontMetricsF& fm, const QSizeF& imageSize);

// 定义文本 → 分类（库内命中用其分类，否则归特殊功能）
QString defCategory(const Project& project, const QString& text);

}  // namespace pinout
