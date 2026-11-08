#include "app/CrashGuard.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define BC_HAVE_BACKTRACE 1
#endif
#endif

namespace bc {
namespace crash {
namespace {

QString g_logDir;
QString g_sessionFile;
QString g_sessionLog;
bool g_previousCrashed = false;
std::atomic<int> g_swallowed{0};
QMutex g_logMutex;

// Pre-built so the crash handlers never allocate.
char g_crashPath[1024] = {0};
char g_version[32] = {0};

QString timestamp() { return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")); }

void writeReportHeader(FILE* f, const char* what)
{
    std::fprintf(f, "Breefer Composer %s crash report\n", g_version);
    std::fprintf(f, "reason: %s\n", what);
    std::fprintf(f, "Please attach this file when reporting the problem:\n");
    std::fprintf(f, "https://github.com/shineakhanda/breefer-composer/issues\n\n");
}

#ifdef Q_OS_WIN
LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info)
{
    static std::atomic<bool> busy{false};
    if (busy.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    FILE* f = std::fopen(g_crashPath, "wb");
    if (f) {
        char reason[96];
        std::snprintf(reason, sizeof(reason), "exception 0x%08lX at %p",
                      static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                      info->ExceptionRecord->ExceptionAddress);
        writeReportHeader(f, reason);
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(info->ExceptionRecord->ExceptionAddress), &module)) {
            char name[MAX_PATH] = {0};
            GetModuleFileNameA(module, name, MAX_PATH);
            std::fprintf(f, "module: %s (+0x%llx)\n", name,
                         static_cast<unsigned long long>(
                             reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress)
                             - reinterpret_cast<uintptr_t>(module)));
        }
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;   // let the OS finish the job
}
#else
void signalHandler(int sig)
{
    const int fd = ::open(g_crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        const char head[] = "Breefer Composer crash report\nPlease attach this file at "
                            "https://github.com/shineakhanda/breefer-composer/issues\n\nsignal: ";
        (void)!::write(fd, head, sizeof(head) - 1);
        const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE"
                         : sig == SIGILL ? "SIGILL" : "SIGBUS/other";
        (void)!::write(fd, name, std::strlen(name));
        (void)!::write(fd, "\n\n", 2);
#ifdef BC_HAVE_BACKTRACE
        void* frames[64];
        const int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, fd);
#endif
        ::close(fd);
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

[[noreturn]] void terminateHandler()
{
    FILE* f = std::fopen(g_crashPath, "wb");
    if (f) {
        const char* what = "std::terminate (unhandled C++ exception)";
        static char buffer[300];
        try {
            if (auto e = std::current_exception()) std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            std::snprintf(buffer, sizeof(buffer), "unhandled exception: %s", ex.what());
            what = buffer;
        } catch (...) {
        }
        writeReportHeader(f, what);
        std::fclose(f);
    }
    std::abort();
}

void rotateLog()
{
    QFileInfo info(g_sessionLog);
    if (info.exists() && info.size() > 512 * 1024) {
        QFile::remove(g_sessionLog + QStringLiteral(".1"));
        QFile::rename(g_sessionLog, g_sessionLog + QStringLiteral(".1"));
    }
}

void pruneOldReports()
{
    QDir dir(g_logDir);
    const QFileInfoList reports = dir.entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
    for (int i = 10; i < reports.size(); ++i) QFile::remove(reports[i].absoluteFilePath());
}

} // namespace

QString logDirectory() { return g_logDir; }

void log(const QString& line)
{
    if (g_sessionLog.isEmpty()) return;
    QMutexLocker lock(&g_logMutex);
    QFile f(g_sessionLog);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream(&f) << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' ' << line << '\n';
}

void noteSwallowed(const QString& what)
{
    ++g_swallowed;
    log(what);
}

int swallowedErrors() { return g_swallowed.load(); }

QString lastCrashReport()
{
    QDir dir(g_logDir);
    const QFileInfoList reports = dir.entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
    return reports.isEmpty() ? QString() : reports.first().absoluteFilePath();
}

bool previousSessionCrashed() { return g_previousCrashed; }

void markCleanExit()
{
    if (!g_sessionFile.isEmpty()) QFile::remove(g_sessionFile);
}

void install()
{
    g_logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(g_logDir);
    g_sessionFile = g_logDir + QStringLiteral("/session.lock");
    g_sessionLog = g_logDir + QStringLiteral("/session.log");
    rotateLog();
    pruneOldReports();

    g_previousCrashed = QFileInfo::exists(g_sessionFile);
    QFile marker(g_sessionFile);
    if (marker.open(QIODevice::WriteOnly)) marker.write(timestamp().toUtf8());

    const QByteArray version = QApplication::applicationVersion().toUtf8();
    std::snprintf(g_version, sizeof(g_version), "%s", version.constData());
    const QByteArray path = QDir::toNativeSeparators(g_logDir + QStringLiteral("/crash-") + timestamp()
                                                     + QStringLiteral(".txt")).toLocal8Bit();
    std::snprintf(g_crashPath, sizeof(g_crashPath), "%s", path.constData());

    std::set_terminate(terminateHandler);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledFilter);
#else
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL
#ifdef SIGBUS
                    , SIGBUS
#endif
         })
        std::signal(sig, signalHandler);
#endif
    log(QStringLiteral("session start, version %1, %2 logical cores")
            .arg(QApplication::applicationVersion()).arg(QThread::idealThreadCount()));
}

} // namespace crash

bool GuardedApplication::notify(QObject* receiver, QEvent* event)
{
    try {
        return QApplication::notify(receiver, event);
    } catch (const std::bad_alloc&) {
        crash::noteSwallowed(QStringLiteral("out of memory in an event handler (recovered)"));
    } catch (const std::exception& e) {
        crash::noteSwallowed(QStringLiteral("exception in event handler (recovered): %1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        crash::noteSwallowed(QStringLiteral("unknown exception in event handler (recovered)"));
    }
    return false;
}

} // namespace bc
