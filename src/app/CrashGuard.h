#pragma once

#include <QApplication>
#include <QString>

namespace bc {

// Crash containment for the desktop app.
//  * Native crashes (access violation, abort, illegal instruction ...) write a report to
//    <AppData>/logs/crash-<time>.txt before the process dies.
//  * A "session" marker file detects an unclean exit so the next start can say so and point the
//    user at the auto-saved work.
//  * Exceptions that escape a Qt slot/event handler are caught and logged instead of killing the
//    editor, and the GUI keeps running.
namespace crash {

void install();                       // call once, right after QApplication exists
QString logDirectory();
QString lastCrashReport();            // path of the newest crash report, or empty
bool previousSessionCrashed();        // true when the last run did not exit cleanly
void markCleanExit();                 // call when the event loop returned normally
void log(const QString& line);        // append to the session log (rotated, bounded size)
void noteSwallowed(const QString& what);

// Number of recoverable errors swallowed so far this session.
int swallowedErrors();

} // namespace crash

// QApplication that survives exceptions thrown from slots and event handlers.
class GuardedApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override;
};

} // namespace bc
