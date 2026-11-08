#pragma once

#include "core/Project.h"

#include <QObject>
#include <QString>

namespace bc {

class ProxyWorker;

// Background proxy generation. Encodes an intra-frame MJPEG proxy at a lower
// resolution so scrubbing a 4K source on a 4-thread CPU stays cheap.
class ProxyManager : public QObject {
    Q_OBJECT

public:
    explicit ProxyManager(QObject* parent = nullptr);
    ~ProxyManager() override;

    static QString proxyDirectory();
    static QString proxyPathFor(const MediaRef& ref, int targetHeight);

    void enqueue(const MediaRef& ref, int targetHeight);
    int pendingCount() const;

signals:
    void progress(const QString& mediaId, int percent);
    void finished(const QString& mediaId, const QString& proxyPath);
    void failed(const QString& mediaId, const QString& error);

private:
    ProxyWorker* m_worker = nullptr;
};

} // namespace bc
