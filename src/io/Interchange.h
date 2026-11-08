#pragma once

#include "core/Project.h"

#include <QString>

namespace bc::io {

// One adapter per format; the canonical model never changes shape for them.
bool exportEdl(const Sequence& sequence, const Project& project,
               const QString& path, QString* error);

bool exportFcp7Xml(const Sequence& sequence, const Project& project,
                   const QString& path, QString* error);

bool exportFcpXml(const Sequence& sequence, const Project& project,
                  const QString& path, QString* error);

bool exportOtio(const Sequence& sequence, const Project& project,
                const QString& path, QString* error);

// Reads OpenTimelineIO (.otio), Final Cut Pro 7 XML (xmeml, what Premiere Pro
// and Resolve export) or CMX 3600 EDL into a new sequence. Referenced media is
// returned unprobed (paths, names, guessed streams); the caller probes it.
bool importTimeline(const QString& path, Sequence& out, QVector<MediaRef>& media,
                    QStringList* warnings, QString* error);

} // namespace bc::io
