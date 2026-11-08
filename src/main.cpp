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
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    av_log_set_level(AV_LOG_ERROR);
    bc::theme::apply(app);

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
