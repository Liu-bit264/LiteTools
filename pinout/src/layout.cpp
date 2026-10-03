#include "layout.h"

#include <algorithm>
#include <limits>

namespace pinout {

QString defCategory(const Project& project, const QString& text)
{
    for (const LibraryEntry& e : project.library)
        if (e.text == text)
            return e.category;
    return QStringLiteral("special");
}

static qreal chipWidth(const QFontMetricsF& fm, const QString& text)
{
    return std::max(kMinChipWidth, fm.horizontalAdvance(text) + 2 * kChipPadX);
}

static QColor categoryColor(const Project& project, const QString& id, const QColor& fallback)
{
    auto it = project.categories.constFind(id);
    return it == project.categories.constEnd() ? fallback : it->color;
}

ChainLayout layoutChain(const Project& project, const Pin& pin,
                        const QFontMetricsF& fm, const QSizeF& imageSize)
{
    ChainLayout out;
    const bool left = (pin.side == QStringLiteral("left"));
    const qreal midY = pin.pos.y();

    // 芯片内容序列：名称芯片 + 定义芯片（或 "-" 占位）
    QVector<std::pair<QString, QColor>> texts;
    texts.append(std::make_pair(pin.name.isEmpty() ? QStringLiteral("?") : pin.name,
                                categoryColor(project, pin.category, QColor(0x8E, 0x24, 0xAA))));
    if (pin.defs.isEmpty()) {
        texts.append(std::make_pair(QStringLiteral("-"), QColor(0x90, 0x90, 0x90)));
    } else {
        for (const QString& d : pin.defs)
            texts.append(std::make_pair(
                d, categoryColor(project, defCategory(project, d), QColor(0x8E, 0x24, 0xAA))));
    }

    // 从板图边缘向外侧依次排布
    const qreal startEdge = left ? -kBoardGap : imageSize.width() + kBoardGap;
    qreal cursor = startEdge;  // 下一枚芯片朝向板图一侧的边（left：右边缘；right：左边缘）
    for (int i = 0; i < texts.size(); ++i) {
        Chip chip;
        chip.text = texts[i].first;
        chip.color = texts[i].second;
        chip.placeholder = (i > 0 && pin.defs.isEmpty());
        const qreal w = chipWidth(fm, chip.text);
        if (left) {
            cursor -= w;
            chip.rect = QRectF(cursor, midY - kChipHeight / 2.0, w, kChipHeight);
            cursor -= kChipGap;
        } else {
            chip.rect = QRectF(cursor, midY - kChipHeight / 2.0, w, kChipHeight);
            cursor += w + kChipGap;
        }
        out.chips.append(chip);
    }

    // 虚线：引脚 → 名称芯片，以及相邻芯片之间的间隙段
    for (int i = 0; i < out.chips.size(); ++i) {
        const QRectF& r = out.chips[i].rect;
        if (i == 0) {
            out.leaders.append(QLineF(pin.pos, QPointF(left ? r.right() : r.left(), midY)));
        } else {
            const QRectF& prev = out.chips[i - 1].rect;
            out.leaders.append(left ? QLineF(r.right(), midY, prev.left(), midY)
                                    : QLineF(prev.right(), midY, r.left(), midY));
        }
    }

    // 引脚点计入包围盒：QRectF.contains 不含右/下边界，构造时留 0.5px 余量
    out.bounds = QRectF(pin.pos.x() - 0.5, pin.pos.y() - 0.5, 1.0, 1.0);
    for (const Chip& c : out.chips)
        out.bounds = out.bounds.united(c.rect);
    return out;
}

LegendLayout layoutLegend(const Project& project, const QFontMetricsF& fm,
                          const QRectF& imageRect)
{
    LegendLayout out;
    const QStringList ids = project.orderedCategoryIds();
    if (ids.isEmpty()) {
        out.bounds = QRectF();
        return out;
    }

    // 每个条目的宽度（色块 + 间隙 + 文字），按预设顺序前一半进第一行、其余进第二行
    struct Entry {
        CategoryDef def;
        qreal width;
    };
    QVector<Entry> entries;
    for (const QString& id : ids) {
        const CategoryDef& c = project.categories[id];
        const qreal w = kLegendSwatch + 4.0 + fm.horizontalAdvance(c.label) + 2 * kLegendPadX;
        entries.append(Entry{c, w});
    }
    const int row1 = std::min<int>(entries.size(), (entries.size() + 1) / 2);

    qreal y = imageRect.bottom() + kLegendTopGap;
    qreal minX = (std::numeric_limits<qreal>::max)();
    qreal maxX = (std::numeric_limits<qreal>::lowest)();
    qreal maxY = y;
    int idx = 0;
    for (int r = 0; r < 2; ++r) {
        const int count = (r == 0) ? row1 : (entries.size() - row1);
        if (count <= 0)
            break;
        qreal rowWidth = 0;
        for (int i = 0; i < count; ++i)
            rowWidth += entries[idx + i].width + (i ? kLegendGap : 0);
        qreal x = imageRect.center().x() - rowWidth / 2.0;
        QVector<std::pair<CategoryDef, QRectF>> rowRects;
        QVector<QRectF> rowSwatches;
        for (int i = 0; i < count; ++i) {
            const Entry& e = entries[idx + i];
            const QRectF full(x, y, e.width, kChipHeight);
            rowRects.append(std::make_pair(e.def, full));
            rowSwatches.append(QRectF(x + kLegendPadX, y + (kChipHeight - kLegendSwatch) / 2.0,
                                      kLegendSwatch, kLegendSwatch));
            x += e.width + kLegendGap;
            minX = std::min(minX, full.left());
            maxX = (std::max)(maxX, full.right());
        }
        out.rows.append(rowRects);
        out.swatches.append(rowSwatches);
        idx += count;
        maxY = y + kChipHeight;
        y += kChipHeight + kLegendRowGap;
    }

    if (!out.rows.isEmpty())
    out.bounds = QRectF(minX, imageRect.bottom() + kLegendTopGap,
                        std::max<qreal>(0, maxX - minX),
                        std::max<qreal>(0, maxY - (imageRect.bottom() + kLegendTopGap)));
    return out;
}

QRectF posterBounds(const Project& project, const QFontMetricsF& fm, const QSizeF& imageSize)
{
    const QRectF imageRect(0, 0, imageSize.width(), imageSize.height());
    QRectF bounds = imageRect;
    for (const Pin& p : project.pins) {
        const ChainLayout c = layoutChain(project, p, fm, imageSize);
        bounds = bounds.united(c.bounds);
    }
    const LegendLayout legend = layoutLegend(project, fm, imageRect);
    if (legend.bounds.isValid())
        bounds = bounds.united(legend.bounds);
    return bounds.adjusted(-kPosterMargin, -kPosterMargin, kPosterMargin, kPosterMargin);
}

}  // namespace pinout
