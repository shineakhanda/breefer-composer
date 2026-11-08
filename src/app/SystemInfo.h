#pragma once

#include <QString>
#include <QStringList>

namespace bc::sys {

struct Memory {
    qint64 totalMB = 0;
    qint64 availableMB = 0;   // free + reclaimable; 0 when the OS cannot tell
};

Memory memory();
QString cpuName();
QString osName();

// FFmpeg hardware back-ends compiled into this build (d3d11va, vaapi, videotoolbox, cuda ...).
QStringList hardwareDecoders();
// Hardware video encoders FFmpeg can see (h264_nvenc, h264_qsv, h264_amf, h264_videotoolbox ...).
QStringList hardwareEncoders();

// Multi-line report for Help > System Information and bug reports.
QString report();

// Frame-cache budget that suits the free RAM: generous on big machines, small on 4 GB laptops.
int suggestedCacheKB(bool lowEnd);

} // namespace bc::sys
