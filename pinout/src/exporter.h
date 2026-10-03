#pragma once
// 海报导出：按工程渲染"图一"风格 PNG（网格底 + 居中板图 + 芯片链 + 底部图例）。
// 渲染与画布共用 layout 模块，超采样倍率 scale 下整幅以世界坐标绘制。

#include "project.h"

#include <QImage>
#include <QString>

namespace pinout {

class Exporter {
public:
    static constexpr qreal kDefaultScale = 2.0;

    // scale：超采样倍率（2.0 = 每世界像素 2 设备像素）；失败返回空图并写 error
    static QImage renderPoster(const Project& project, const QPixmap& boardImage,
                               qreal scale, QString* error = nullptr);
    // 渲染并写 PNG：覆盖前自动 .bak 备份 + QSaveFile 原子写入；
    // 工程内 imageSize 与板图不符时回填并重存工程
    static bool exportPng(Project& project, const QString& outputPng,
                          qreal scale, QString* error = nullptr);
};

}  // namespace pinout
