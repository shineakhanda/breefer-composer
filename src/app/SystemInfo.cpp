#include "app/SystemInfo.h"

#include <QSysInfo>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <unistd.h>
#include <QFile>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
}

namespace bc::sys {

Memory memory()
{
    Memory m;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        m.totalMB = static_cast<qint64>(status.ullTotalPhys / (1024 * 1024));
        m.availableMB = static_cast<qint64>(status.ullAvailPhys / (1024 * 1024));
    }
#elif defined(Q_OS_MACOS)
    int64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) == 0) m.totalMB = total / (1024 * 1024);
    vm_statistics64_data_t stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS) {
        const qint64 page = static_cast<qint64>(sysconf(_SC_PAGESIZE));
        m.availableMB = (static_cast<qint64>(stats.free_count) + static_cast<qint64>(stats.inactive_count)) * page / (1024 * 1024);
    }
#else
    QFile f(QStringLiteral("/proc/meminfo"));
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray& line : lines) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() < 2) continue;
            if (parts[0] == "MemTotal:") m.totalMB = parts[1].toLongLong() / 1024;
            else if (parts[0] == "MemAvailable:") m.availableMB = parts[1].toLongLong() / 1024;
        }
    }
#endif
    return m;
}

QString cpuName()
{
    const QString name = QSysInfo::currentCpuArchitecture();
    return QStringLiteral("%1, %2 logical cores").arg(name).arg(QThread::idealThreadCount());
}

QString osName() { return QSysInfo::prettyProductName(); }

QStringList hardwareDecoders()
{
    QStringList list;
    for (AVHWDeviceType t = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE); t != AV_HWDEVICE_TYPE_NONE;
         t = av_hwdevice_iterate_types(t))
        list << QString::fromLatin1(av_hwdevice_get_type_name(t));
    return list;
}

QStringList hardwareEncoders()
{
    QStringList list;
    for (const char* name : {"h264_nvenc", "hevc_nvenc", "av1_nvenc", "h264_qsv", "hevc_qsv", "av1_qsv", "h264_amf",
                             "hevc_amf", "av1_amf", "h264_videotoolbox", "hevc_videotoolbox", "h264_vaapi",
                             "hevc_vaapi", "h264_mf", "hevc_mf"})
        if (avcodec_find_encoder_by_name(name)) list << QString::fromLatin1(name);
    return list;
}

int suggestedCacheKB(bool lowEnd)
{
    const Memory m = memory();
    if (m.totalMB <= 0) return lowEnd ? 96 * 1024 : 256 * 1024;
    // A sixth of what is free, never more than an eighth of all RAM, and a hard ceiling.
    qint64 mb = (m.availableMB > 0 ? m.availableMB : m.totalMB / 2) / 6;
    mb = std::min<qint64>(mb, m.totalMB / 8);
    mb = std::min<qint64>(mb, lowEnd ? 128 : 1024);
    mb = std::max<qint64>(mb, 48);
    return static_cast<int>(mb * 1024);
}

QString report()
{
    const Memory m = memory();
    QStringList lines;
    lines << QStringLiteral("System: %1").arg(osName());
    lines << QStringLiteral("CPU: %1").arg(cpuName());
    lines << QStringLiteral("Memory: %1 MB total, %2 MB available").arg(m.totalMB).arg(m.availableMB);
    const QStringList dec = hardwareDecoders();
    const QStringList enc = hardwareEncoders();
    lines << QStringLiteral("Hardware decode back-ends: %1").arg(dec.isEmpty() ? QStringLiteral("none") : dec.join(QStringLiteral(", ")));
    lines << QStringLiteral("Hardware encoders: %1").arg(enc.isEmpty() ? QStringLiteral("none (software encoding)") : enc.join(QStringLiteral(", ")));
    lines << QStringLiteral("Preview compositing: CPU (multi-threaded); a missing or old GPU never affects it");
    return lines.join(QLatin1Char('\n'));
}

} // namespace bc::sys
