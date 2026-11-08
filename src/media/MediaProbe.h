#pragma once

#include "core/Project.h"
#include <QImage>
#include <QString>

namespace bc {

struct ProbeResult {
    bool ok = false;
    QString error;
    MediaRef ref;
};

ProbeResult probeMedia(const QString& path);

// Decodes one frame for the bin; returns a null image on failure.
QImage extractThumbnail(const QString& path, int maxWidth, int maxHeight);

} // namespace bc
