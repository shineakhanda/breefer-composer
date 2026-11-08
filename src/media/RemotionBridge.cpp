#include "media/RemotionBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace bc {

RemotionBridge::RemotionBridge(QObject* parent)
    : QObject(parent)
{
}

QString RemotionBridge::nodeExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

QString RemotionBridge::npxExecutable()
{
#ifdef Q_OS_WIN
    QString npx = QStandardPaths::findExecutable(QStringLiteral("npx.cmd"));
    if (npx.isEmpty()) npx = QStandardPaths::findExecutable(QStringLiteral("npx"));
    return npx;
#else
    return QStandardPaths::findExecutable(QStringLiteral("npx"));
#endif
}

bool RemotionBridge::isRemotionFolder(const QString& folder, QString* kind)
{
    const QDir dir(folder);
    // A bundle has index.html + bundle.js produced by `remotion bundle`.
    if (QFileInfo::exists(dir.filePath(QStringLiteral("index.html"))) && QFileInfo::exists(dir.filePath(QStringLiteral("bundle.js")))) {
        if (kind) *kind = QStringLiteral("bundle");
        return true;
    }
    QFile pkg(dir.filePath(QStringLiteral("package.json")));
    if (pkg.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(pkg.readAll()).object();
        const QJsonObject deps = o.value(QStringLiteral("dependencies")).toObject();
        const QJsonObject dev = o.value(QStringLiteral("devDependencies")).toObject();
        if (deps.contains(QStringLiteral("remotion")) || dev.contains(QStringLiteral("remotion"))
            || deps.contains(QStringLiteral("@remotion/cli")) || dev.contains(QStringLiteral("@remotion/cli"))) {
            if (kind) *kind = o.contains(QStringLiteral("breefer")) ? QStringLiteral("react-plugin") : QStringLiteral("project");
            return true;
        }
    }
    return false;
}

QStringList RemotionBridge::baseArgs(const QString& project, const QString& entry) const
{
    QStringList args{QStringLiteral("--yes"), QStringLiteral("remotion")};
    Q_UNUSED(project);
    Q_UNUSED(entry);
    return args;
}

QStringList RemotionBridge::listCompositions(const QString& project, const QString& entry, QString* error)
{
    const QString npx = npxExecutable();
    if (npx.isEmpty()) {
        if (error) *error = tr("Node.js (npx) was not found. Install Node.js 18+ from nodejs.org.");
        return {};
    }
    QProcess p;
    p.setWorkingDirectory(project);
    QStringList args = baseArgs(project, entry);
    args << QStringLiteral("compositions");
    QString kind;
    isRemotionFolder(project, &kind);
    if (kind == QLatin1String("bundle")) args << project;
    else if (!entry.isEmpty()) args << entry;
    args << QStringLiteral("--quiet");
    p.start(npx, args);
    if (!p.waitForFinished(120000)) {
        if (error) *error = tr("Timed out listing compositions");
        p.kill();
        return {};
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (p.exitCode() != 0) {
        if (error) *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        return {};
    }
    // --quiet prints the ids separated by spaces/newlines.
    QStringList ids;
    for (const QString& token : out.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
        if (QRegularExpression(QStringLiteral("^[A-Za-z0-9_\\-]+$")).match(token).hasMatch()) ids << token;
    return ids;
}

void RemotionBridge::render(const Request& r)
{
    if (isBusy()) {
        emit failed(tr("A Remotion render is already running"));
        return;
    }
    const QString npx = npxExecutable();
    if (npx.isEmpty()) {
        emit failed(tr("Node.js (npx) was not found. Install Node.js 18+ from nodejs.org, then try again."));
        return;
    }
    QDir().mkpath(QFileInfo(r.output).absolutePath());

    QStringList args = baseArgs(r.project, r.entry);
    args << QStringLiteral("render");
    QString kind;
    isRemotionFolder(r.project, &kind);
    if (kind == QLatin1String("bundle")) args << r.project;
    else if (!r.entry.isEmpty()) args << r.entry;
    args << r.composition << r.output;
    // ProRes 4444 keeps the alpha channel for overlays.
    args << QStringLiteral("--codec=prores") << QStringLiteral("--prores-profile=4444")
         << QStringLiteral("--image-format=png") << QStringLiteral("--pixel-format=yuva444p10le");
    if (!r.propsJson.trimmed().isEmpty()) {
        // Write props to a file: avoids shell quoting problems on Windows.
        const QString propsPath = QFileInfo(r.output).absolutePath() + QStringLiteral("/props-")
                                  + QFileInfo(r.output).completeBaseName() + QStringLiteral(".json");
        QFile f(propsPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(r.propsJson.toUtf8());
            f.close();
            args << QStringLiteral("--props=") + propsPath;
        }
    }

    m_output = r.output;
    m_log.clear();
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(kind == QLatin1String("bundle") ? QFileInfo(r.project).absolutePath() : r.project);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        const QString chunk = QString::fromUtf8(m_process->readAll());
        m_log += chunk;
        if (m_log.size() > 20000) m_log = m_log.right(20000);
        // "Rendered 45/150" or "Encoded 45/150"
        static const QRegularExpression re(QStringLiteral("(Rendered|Encoded|Rendering frames)[^0-9]*(\\d+)/(\\d+)"));
        auto it = re.globalMatch(chunk);
        while (it.hasNext()) {
            const auto m = it.next();
            const int done = m.captured(2).toInt();
            const int total = std::max(1, m.captured(3).toInt());
            const bool encoding = m.captured(1) == QLatin1String("Encoded");
            emit progress((encoding ? 50 : 0) + done * 50 / total, chunk.trimmed().section(QLatin1Char('\n'), -1));
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        QProcess* p = m_process;
        m_process = nullptr;
        p->deleteLater();
        if (status == QProcess::NormalExit && code == 0 && QFileInfo::exists(m_output)) emit finished(m_output);
        else emit failed(tr("Remotion render failed (exit %1):\n%2").arg(code).arg(m_log.right(1500)));
    });
    m_process->start(npx, args);
    emit progress(0, tr("Starting Remotion (first run downloads Chrome Headless Shell)..."));
}

void RemotionBridge::cancel()
{
    if (m_process) m_process->kill();
}

bool RemotionBridge::isBusy() const
{
    return m_process != nullptr;
}

void RemotionBridge::openStudio(const QString& project, const QString& entry)
{
    const QString npx = npxExecutable();
    if (npx.isEmpty()) {
        emit failed(tr("Node.js (npx) was not found. Install Node.js 18+ from nodejs.org."));
        return;
    }
    QStringList args = baseArgs(project, entry);
    args << QStringLiteral("studio");
    if (!entry.isEmpty()) args << entry;
    QProcess::startDetached(npx, args, project);
}

} // namespace bc
