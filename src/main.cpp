#include "engine/Compositor.h"
#include "engine/PluginHost.h"
#include "io/ProjectIO.h"

#include <cstdio>
#include <cstdlib>
#include "app/CrashGuard.h"
#include "ui/MainWindow.h"
#include "ui/StartDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QFile>
#include <QThread>
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

namespace {
// On 1024x600 netbooks and old MacBooks a dialog taller than the screen cannot be reached; this keeps
// every dialog inside the monitor it opens on.
class DialogFitter : public QObject {
public:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() != QEvent::Show) return false;
        auto* dialog = qobject_cast<QDialog*>(object);
        if (!dialog || !dialog->isWindow()) return false;
        const QScreen* screen = dialog->screen() ? dialog->screen() : QGuiApplication::primaryScreen();
        if (!screen) return false;
        const QRect avail = screen->availableGeometry();
        const QSize limit(avail.width() * 96 / 100, avail.height() * 96 / 100);
        dialog->setMinimumSize(std::min(dialog->minimumWidth(), limit.width()), std::min(dialog->minimumHeight(), limit.height()));
        if (dialog->width() > limit.width() || dialog->height() > limit.height()) dialog->resize(dialog->size().boundedTo(limit));
        return false;
    }
};
} // namespace

int main(int argc, char** argv)
{
    bc::GuardedApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Breefer Composer"));
    app.setOrganizationName(QStringLiteral("Breefer"));
    app.setApplicationVersion(QStringLiteral(BREEFER_VERSION));

    app.installEventFilter(new DialogFitter);
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

    // Layout check: breefer --screenshot <out.png> <WxH> [project]  (renders the editor window
    // to a PNG at the given size; used to verify small-screen and portrait layouts).
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--screenshot")) {
        const QStringList size = QString::fromLocal8Bit(argv[3]).split(QLatin1Char('x'));
        bc::MainWindow shot;
        shot.resize(size.value(0).toInt(), size.value(1).toInt());
        shot.showNormal();
        shot.resize(size.value(0).toInt(), size.value(1).toInt());
        if (argc >= 5 && !shot.openProjectFile(QString::fromLocal8Bit(argv[4]))) {
            bc::Project probe;
            QString why;
            bc::io::loadProject(QString::fromLocal8Bit(argv[4]), probe, &why);
            QFile f(QStringLiteral("shot_error.txt"));
            if (f.open(QIODevice::WriteOnly)) f.write(why.toUtf8());
            return 4;
        }
        for (int i = 0; i < 12; ++i) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(40);
        }
        const QSize asked(size.value(0).toInt(), size.value(1).toInt());
        shot.resize(asked);
        QApplication::processEvents();
        const bool ok = shot.grab().save(QString::fromLocal8Bit(argv[2]));
        std::fprintf(stdout, "window %dx%d, minimum %dx%d\n", shot.width(), shot.height(),
                     shot.minimumSizeHint().width(), shot.minimumSizeHint().height());
        return ok ? 0 : 3;
    }

    bc::crash::install();

    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--crash-test")) {
        // Deliberate access violation: proves the crash report and session marker work.
        volatile int* nothing = nullptr;
        *nothing = 1;
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

    if (bc::crash::previousSessionCrashed()) {
        QTimer::singleShot(600, &window, [&window] { window.reportPreviousCrash(bc::crash::lastCrashReport()); });
    }
    const int code = app.exec();
    bc::crash::markCleanExit();
    return code;
}
