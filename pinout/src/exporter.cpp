#include "exporter.h"

#include "chiprender.h"
#include "layout.h"

#include <QBuffer>
#include <QSaveFile>
#include <QVector>
#include <QPainter>

namespace pinout {

QImage Exporter::renderPoster(const Project& project, const QPixmap& boardImage,
                              qreal scale, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return QImage();
    };
    if (scale <= 0 || scale > 8)
        return fail(QStringLiteral("scale 必须在 (0, 8] 内"));
    if (boardImage.isNull())
        return fail(QStringLiteral("板图为空"));
    if (project.imageSize.isEmpty() || project.imageSize != boardImage.size())
        return fail(QStringLiteral("image_size 与板图尺寸不一致（先在工程中回填板图尺寸）"));

    const QFontMetricsF fm(chipFont());
    const QSizeF size(project.imageSize.width(), project.imageSize.height());
    const QRectF imageRect(0, 0, size.width(), size.height());

    // 单趟计算各引脚芯片链（避免 posterBounds 与绘制各算一遍）
    QVector<ChainLayout> chains;
    chains.reserve(project.pins.size());
    QRectF content = imageRect;
    for (const Pin& p : project.pins) {
        chains.append(layoutChain(project, p, fm, size));
        content = content.united(chains.last().bounds);
    }
    const LegendLayout legend = layoutLegend(project, fm, imageRect);
    if (legend.bounds.isValid())
        content = content.united(legend.bounds);
    const QRectF bounds = content.adjusted(-kPosterMargin, -kPosterMargin,
                                           kPosterMargin, kPosterMargin);

    const QSize device(int(bounds.width() * scale), int(bounds.height() * scale));
    if (device.width() <= 0 || device.height() <= 0 || device.width() > 16384
        || device.height() > 16384)
        return fail(QStringLiteral("导出尺寸异常：%1x%2").arg(device.width()).arg(device.height()));

    QImage image(device, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0xE8, 0xF0, 0xF7));  // 蓝图底色

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    painter.scale(scale, scale);
    painter.translate(-bounds.topLeft());

    drawGrid(painter, bounds);
    painter.drawPixmap(QPointF(0, 0), boardImage);
    for (const ChainLayout& chain : chains)
        drawChain(painter, chain);
    drawLegend(painter, legend);

    painter.end();
    return image;
}

bool Exporter::exportPng(Project& project, const QString& outputPng, qreal scale, QString* error)
{
    const QString imgPath = project.absoluteImagePath();
    QPixmap board(imgPath);
    if (board.isNull()) {
        if (error)
            *error = QStringLiteral("无法加载板图：%1").arg(imgPath);
        return false;
    }
    // imageSize 与板图实际尺寸不一致时按板图回填并重存工程（导出前自洽）
    if (project.imageSize != board.size()) {
        project.imageSize = board.size();
        if (!project.save(error))
            return false;  // 工程回写失败必须暴露，避免静默滞留旧尺寸
    }

    const QImage poster = renderPoster(project, board, scale, error);
    if (poster.isNull())
        return false;

    if (!Project::backupFile(outputPng, error))
        return false;

    QByteArray png;
    QBuffer buf(&png);
    if (!buf.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("内部缓冲打开失败");
        return false;
    }
    if (!poster.save(&buf, "PNG")) {
        if (error)
            *error = QStringLiteral("PNG 编码失败");
        return false;
    }

    QSaveFile f(outputPng);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入导出文件：%1").arg(outputPng);
        return false;
    }
    if (f.write(png) != png.size()) {
        if (error)
            *error = QStringLiteral("导出文件写入不完整：%1").arg(outputPng);
        return false;
    }
    if (!f.commit()) {
        if (error)
            *error = QStringLiteral("导出文件提交失败：%1").arg(outputPng);
        return false;
    }
    return true;
}

}  // namespace pinout
