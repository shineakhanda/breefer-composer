#include "engine/Compositor.h"
#include "engine/PluginHost.h"
#include "io/ProjectIO.h"

#include <cstdio>
#include <cstdlib>
#include "ui/MainWindow.h"
#include "ui/StartDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

extern "C" {
#include <libavutil/log.h>
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Breefer Composer"));
    app.setOrganizationName(QStringLiteral("Breefer"));
    app.setApplicationVersion(QStringLiteral(BREEFER_VERSION));

    av_log_set_level(AV_LOG_ERROR);
    bc::theme::apply(app);
    bc::PluginHost::instance();   // scan plugins before any render thread starts

    // Headless render: breefer --render-frame <project.bcproj> <seconds> <out.png> [width]
    // Renders the first sequence at full quality from originals; used for
    // scripted thumbnails, CI checks and debugging.
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--render-frame")) {
        bc::Project project;
        QString error;
        if (!bc::io::loadProject(QString::fromLocal8Bit(argv[2]), project, &error) || project.sequences.isEmpty()) {
            std::fprintf(stderr, "cannot load project: %s\n", qPrintable(error));
            return 2;
        }
        const bc::Sequence& seq = project.sequences.first();
        const int width = argc >= 6 ? std::atoi(argv[5]) : seq.width;
        bc::Compositor compositor;
        compositor.setPreviewSize(width, std::max(2, width * seq.height / std::max(1, seq.width)));
        compositor.setHighQuality(true);
        compositor.setLibrary(project.sequences);
        for (auto& m : project.media) m.proxyPath.clear();
        const QImage frame = compositor.render(seq, project.media, bc::secToTicks(std::atof(argv[3])));
        if (!frame.convertToFormat(QImage::Format_RGB32).save(QString::fromLocal8Bit(argv[4]))) {
            std::fprintf(stderr, "cannot write %s\n", argv[4]);
            return 3;
        }
        return 0;
    }

    // Command-line project opens straight into the editor; otherwise the Home
    // screen decides what to do.
    QString projectToOpen;
    QString newName;
    QString newFolder;

    if (argc > 1) {
        projectToOpen = QString::fromLocal8Bit(argv[1]);
    } else {
        bc::StartDialog start;
        if (start.exec() != QDialog::Accepted) return 0;
        projectToOpen = start.projectToOpen();
        newName = start.newProjectName();
        newFolder = start.newProjectFolder();
    }

    bc::MainWindow window;
    window.show();

#ifdef Q_OS_WIN
    {
        HWND handle = reinterpret_cast<HWND>(window.winId());
        auto applyDarkChrome = [handle] {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
            DwmSetWindowAttribute(handle, 19, &dark, sizeof(dark));
            const COLORREF borderColor = RGB(0x1c, 0x1c, 0x1f);
            DwmSetWindowAttribute(handle, 34, &borderColor, sizeof(borderColor));
        };
        applyDarkChrome();
        QTimer::singleShot(60, &window, applyDarkChrome);
    }
#endif

    if (!projectToOpen.isEmpty()) {
        if (!window.openProjectFile(projectToOpen)) return 1;
    } else if (!newName.isEmpty()) {
        window.createProject(newName, newFolder);
    }

    return app.exec();
}
