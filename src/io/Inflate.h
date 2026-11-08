#pragma once

#include <QByteArray>
#include <QString>

namespace bc::io {

// Minimal RFC 1951 (DEFLATE) + RFC 1952 (gzip) decoder. Self-contained so the
// core library does not need zlib; used for Premiere .prproj files.
bool gunzip(const QByteArray& input, QByteArray& output, QString* error = nullptr);

// Raw DEFLATE stream (no header).
bool inflateRaw(const QByteArray& input, int offset, QByteArray& output, QString* error = nullptr);

} // namespace bc::io
