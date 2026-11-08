#include "engine/PluginHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QStandardPaths>

// Mirror of the C ABI in sdk/include/breefer_plugin.h (kept binary-compatible).
extern "C" {
struct BcParamDesc { const char* id; const char* name; float minimum; float maximum; float defaultValue; };
struct BcEffectDesc { int apiVersion; const char* id; const char* name; const char* category; int paramCount; const BcParamDesc* params; };
struct BcImage { int width; int height; int stride; unsigned char* pixels; };
}

namespace bc {

PluginHost& PluginHost::instance()
{
    static PluginHost host;
    static bool scanned = false;
    if (!scanned) {
        scanned = true;
        host.scan();
    }
    return host;
}

QStringList PluginHost::folders() const
{
    QStringList dirs;
    dirs << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
    dirs << QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
    const QByteArray extra = qgetenv("BREEFER_PLUGIN_PATH");
    if (!extra.isEmpty()) dirs << QString::fromLocal8Bit(extra).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return dirs;
}

void PluginHost::scan()
{
    m_effects.clear();
    m_effectLib.clear();
    m_errors.clear();
    for (auto& lib : m_libs) {
        lib.library->unload();
        delete lib.library;
    }
    m_libs.clear();

    for (const QString& dir : folders()) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.dll"), QStringLiteral("*.so"), QStringLiteral("*.dylib")},
                                                           QDir::Files);
        for (const QFileInfo& info : files) {
            auto* library = new QLibrary(info.absoluteFilePath());
            if (!library->load()) {
                m_errors << QStringLiteral("%1: %2").arg(info.fileName(), library->errorString());
                delete library;
                continue;
            }
            using CountFn = int (*)();
            using DescribeFn = const BcEffectDesc* (*)(int);
            using RenderFn = int (*)(int, void*, const float*, double);
            auto count = reinterpret_cast<CountFn>(library->resolve("breefer_plugin_count"));
            auto describe = reinterpret_cast<DescribeFn>(library->resolve("breefer_plugin_describe"));
            auto render = reinterpret_cast<RenderFn>(library->resolve("breefer_plugin_render"));
            if (!count || !describe || !render) {
                m_errors << QStringLiteral("%1: not a Breefer plugin (missing exports)").arg(info.fileName());
                library->unload();
                delete library;
                continue;
            }
            const int libIndex = m_libs.size();
            m_libs.push_back({library, render});
            const int n = count();
            for (int i = 0; i < n; ++i) {
                const BcEffectDesc* d = describe(i);
                if (!d || d->apiVersion != 1 || !d->id) {
                    m_errors << QStringLiteral("%1: effect %2 uses an unsupported API version").arg(info.fileName()).arg(i);
                    continue;
                }
                Effect e;
                e.type = QStringLiteral("plugin:") + QString::fromUtf8(d->id);
                e.name = QString::fromUtf8(d->name ? d->name : d->id);
                e.category = QString::fromUtf8(d->category ? d->category : "Plugins");
                e.library = info.fileName();
                e.index = i;
                for (int p = 0; p < d->paramCount; ++p) {
                    Param param;
                    param.id = QString::fromUtf8(d->params[p].id);
                    param.name = QString::fromUtf8(d->params[p].name);
                    param.minValue = d->params[p].minimum;
                    param.maxValue = d->params[p].maximum;
                    param.value = d->params[p].defaultValue;
                    e.params.push_back(param);
                }
                m_effects.push_back(e);
                m_effectLib.push_back(libIndex);
            }
        }
    }
}

const PluginHost::Effect* PluginHost::find(const QString& type) const
{
    for (const auto& e : m_effects)
        if (e.type == type) return &e;
    return nullptr;
}

bool PluginHost::render(const EffectInstance& effect, QImage& image, Tick localTime) const
{
    for (int i = 0; i < m_effects.size(); ++i) {
        const Effect& e = m_effects[i];
        if (e.type != effect.type) continue;
        if (image.format() != QImage::Format_ARGB32_Premultiplied)
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QVector<float> values;
        for (const Param& declared : e.params) {
            const Param* p = effect.param(declared.id);
            values << static_cast<float>(p ? p->evaluate(localTime) : declared.value);
        }
        BcImage img{image.width(), image.height(), static_cast<int>(image.bytesPerLine()), image.bits()};
        return m_libs[m_effectLib[i]].render(e.index, &img, values.constData(), ticksToSec(localTime)) == 0;
    }
    return false;
}

} // namespace bc
