#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace bc {

// Drives the Remotion CLI (https://remotion.dev) through Node.js so React
// compositions can be used as clips:
//   - a Remotion project folder (package.json with "remotion"), or
//   - a pre-built bundle folder (output of `npx remotion bundle`), i.e. a
//     "React binary plugin" that ships without source.
// Renders are ProRes 4444 with alpha so graphics composite over video.
class RemotionBridge : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString project;       // folder with package.json, or a bundle folder
        QString entry;         // optional entry point (e.g. src/index.ts)
        QString composition;   // composition id
        QString propsJson;     // input props
        QString output;        // .mov path
    };

    explicit RemotionBridge(QObject* parent = nullptr);

    static QString nodeExecutable();   // empty if Node.js is not installed
    static QString npxExecutable();
    static bool isRemotionFolder(const QString& folder, QString* kind = nullptr);

    // Blocking query (runs `remotion compositions`), a few seconds.
    QStringList listCompositions(const QString& project, const QString& entry, QString* error);

    void render(const Request& request);   // async; emits progress/finished/failed
    void cancel();
    bool isBusy() const;
    void openStudio(const QString& project, const QString& entry);

signals:
    void progress(int percent, const QString& line);
    void finished(const QString& outputPath);
    void failed(const QString& error);

private:
    QStringList baseArgs(const QString& project, const QString& entry) const;
    QProcess* m_process = nullptr;
    QString m_output;
    QString m_log;
};

} // namespace bc
