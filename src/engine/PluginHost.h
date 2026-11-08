#pragma once

#include "core/Project.h"

#include <QImage>
#include <QStringList>
#include <QVector>

class QLibrary;

namespace bc {

// Loads native effect plugins (sdk/include/breefer_plugin.h) from the plugin
// folders. Effects are addressed as "plugin:<id>" in EffectInstance::type.
class PluginHost {
public:
    struct Effect {
        QString type;        // "plugin:<id>"
        QString name;
        QString category;
        QString library;     // file it came from
        int index = 0;
        QVector<Param> params;
    };

    static PluginHost& instance();

    void scan();                         // (re)load every plugin folder
    QStringList folders() const;
    const QVector<Effect>& effects() const { return m_effects; }
    const Effect* find(const QString& type) const;
    QStringList errors() const { return m_errors; }

    // Applies the effect in place; returns false when unavailable.
    bool render(const EffectInstance& effect, QImage& image, Tick localTime) const;

private:
    PluginHost() = default;
    struct Lib {
        QLibrary* library = nullptr;
        int (*render)(int, void*, const float*, double) = nullptr;
    };
    QVector<Effect> m_effects;
    QVector<Lib> m_libs;            // parallel to the library each effect came from
    QVector<int> m_effectLib;       // effect index -> m_libs index
    QStringList m_errors;
};

} // namespace bc
