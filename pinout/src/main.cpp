// LiteTools/pinout —— 开发板引脚定义图制作工具入口。
// 用法：
//   pinout.exe [工程.pinout.json]            # GUI（无参数时先弹起始页）
//   pinout.exe --export 工程.pinout.json [输出.png] [倍率]   # CLI 导出海报（默认 <工程名>.png、2x）
// 退出码：0 成功；1 打开工程/导出失败。

#include "exporter.h"
#include "mainwindow.h"
#include "startpage.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <cstdio>
#include <cstring>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LiteTools"));
    QApplication::setApplicationName(QStringLiteral("pinout"));
    QFont uiFont(QStringLiteral("Microsoft YaHei"));
    uiFont.setPointSize(9);
    app.setFont(uiFont);

    // CLI 导出：与 GUI 共用 exporter，便于脚本化批量出图
    if (argc >= 3 && std::strcmp(argv[1], "--export") == 0) {
        const QString projPath = QString::fromLocal8Bit(argv[2]);
        QString err;
        auto project = pinout::Project::load(projPath, &err);
        if (!project) {
            std::fprintf(stderr, "[pinout] 错误: %s\n", qPrintable(err));
            return 1;
        }
        const QString out = argc >= 4
                                ? QString::fromLocal8Bit(argv[3])
                                : QFileInfo(projPath).absolutePath() + QLatin1Char('/')
                                      + QFileInfo(projPath).completeBaseName() + QStringLiteral(".png");
        const qreal scale = argc >= 5 ? QString::fromLocal8Bit(argv[4]).toDouble()
                                      : pinout::Exporter::kDefaultScale;
        if (!pinout::Exporter::exportPng(*project, out, scale, &err)) {
            std::fprintf(stderr, "[pinout] 错误: %s\n", qPrintable(err));
            return 1;
        }
        std::printf("[OK] 已导出: %s\n", qPrintable(out));
        return 0;
    }

    QString projectPath;
    if (argc > 1) {
        projectPath = QString::fromLocal8Bit(argv[1]);
    } else {
        pinout::StartPage start;
        if (start.exec() != QDialog::Accepted || start.selectedProject().isEmpty())
            return 0;
        projectPath = start.selectedProject();
    }

    pinout::MainWindow window;
    if (!window.openProject(projectPath))
        return 1;
    window.show();
    return app.exec();
}
