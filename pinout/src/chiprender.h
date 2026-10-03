#pragma once
// 画布（QGraphicsScene）与导出（QPainter→QImage）共用的绘制原语。
// 全部以世界坐标绘制：调用方负责 transform（导出为 scale+translate，画布为视图变换）。
// header-only：与 layout.h 常量同源，避免两处实现漂移。

#include "layout.h"

#include <QFont>
#include <QPainter>
#include <cmath>

namespace pinout {

// 芯片文字字体（世界坐标字号，配合 painter scale 超采样）。
// 静态缓存：QFont 按族名构造涉及字体解析，绘制热路径不应重复构造。
inline const QFont& chipFont()
{
    static const QFont f = [] {
        QFont font(QStringLiteral("Microsoft YaHei"));
        font.setPixelSize(11);
        font.setBold(true);
        return font;
    }();
    return f;
}

// 蓝图网格：默认 24 一步小格、120 一步大格（均为二进制精确值，fmod 无累积误差）；
// 画布 LOD 可放大步长只画大格
inline void drawGrid(QPainter& painter, const QRectF& rect,
                     qreal step = 24.0, qreal majorStep = 120.0)
{
    QPen minor(QColor(0xD3, 0xE0, 0xEC));
    QPen major(QColor(0xC3, 0xD4, 0xE4));
    const qreal x0 = qFloor(rect.left() / step) * step;
    const qreal y0 = qFloor(rect.top() / step) * step;
    for (qreal x = x0; x <= rect.right(); x += step) {
        painter.setPen(std::fmod(std::abs(x), majorStep) < 0.5 ? major : minor);
        painter.drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }
    for (qreal y = y0; y <= rect.bottom(); y += step) {
        painter.setPen(std::fmod(std::abs(y), majorStep) < 0.5 ? major : minor);
        painter.drawLine(QLineF(rect.left(), y, rect.right(), y));
    }
}

inline void drawChip(QPainter& painter, const QRectF& rect, const QString& text,
                     const QColor& color)
{
    painter.setPen(color.darker(115));
    painter.setBrush(color);
    painter.drawRect(rect);
    painter.setPen(Qt::white);
    painter.setFont(chipFont());
    painter.drawText(rect, Qt::AlignCenter, text);
}

// 一条引脚的名称/定义芯片链（含虚线引线）
inline void drawChain(QPainter& painter, const ChainLayout& chain)
{
    QPen leader(QColor(0x90, 0xA4, 0xAE));
    leader.setWidthF(1.2);
    leader.setStyle(Qt::DashLine);
    leader.setDashPattern(QVector<qreal>{4, 3});
    painter.setPen(leader);
    painter.setBrush(Qt::NoBrush);
    for (const QLineF& l : chain.leaders)
        painter.drawLine(l);
    for (const Chip& c : chain.chips)
        drawChip(painter, c.rect, c.text, c.color);
}

inline void drawLegend(QPainter& painter, const LegendLayout& legend)
{
    for (const auto& row : legend.rows) {
        for (const auto& entry : row) {
            const CategoryDef& def = entry.first;
            const QRectF& full = entry.second;
            painter.setPen(def.color.darker(115));
            painter.setBrush(def.color);
            painter.drawRoundedRect(full, 3, 3);
            painter.setPen(Qt::white);
            painter.setFont(chipFont());
            painter.drawText(full, Qt::AlignCenter, def.label);
        }
    }
}

}  // namespace pinout
