#include "ui/EffectsPanel.h"

#include "core/Document.h"
#include "engine/Effects.h"
#include "engine/PluginHost.h"
#include "ui/Theme.h"

#include <QLineEdit>
#include <cmath>
#include <QListWidget>
#include <QVBoxLayout>

namespace bc {
namespace {

struct EffectEntry {
    const char* type;
    const char* name;
    const char* category;
};

const EffectEntry kEffects[] = {
    {"transition:1", "Cross Dissolve", "Video Transitions"},
    {"transition:2", "Dip to Black", "Video Transitions"},
    {"transition:3", "Dip to White", "Video Transitions"},
    {"transition:4", "Wipe Left", "Video Transitions"},
    {"transition:5", "Wipe Right", "Video Transitions"},
    {"transition:6", "Push", "Video Transitions"},
    {"transition:7", "Iris Round", "Video Transitions"},
    {"audiotransition:1", "Constant Power", "Audio Transitions"},
    {fx::kMotion, "Motion", "Transform"},
    {fx::kOpacity, "Opacity", "Transform"},
    {fx::kCrop, "Crop", "Transform"},
    {fx::kFlip, "Horizontal Flip", "Transform"},
    {fx::kLumetri, "Lumetri Color", "Color Correction"},
    {fx::kVignette, "Vignette", "Stylize"},
    {fx::kBasic3D, "Basic 3D", "Perspective"},
    {fx::kBlur, "Gaussian Blur", "Blur & Sharpen"},
    {fx::kSharpen, "Sharpen", "Blur & Sharpen"},
    {fx::kMask, "Mask (Shape)", "Masking"},
    {fx::kVolume, "Volume", "Audio Effects"},
};

} // namespace

EffectsPanel::EffectsPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search effects..."));
    m_search->setStyleSheet(QStringLiteral("QLineEdit { margin: 6px; }"));
    root->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: #1d1d20; }"));
    root->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, &EffectsPanel::populate);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item) applyEffect(item->data(Qt::UserRole).toString());
    });

    populate(QString());
}

void EffectsPanel::populate(const QString& filter)
{
    m_list->clear();

    QString currentCategory;
    for (const auto& entry : kEffects) {
        const QString name = QString::fromLatin1(entry.name);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive)) continue;

        const QString category = QString::fromLatin1(entry.category);
        if (category != currentCategory) {
            currentCategory = category;
            auto* header = new QListWidgetItem(category.toUpper(), m_list);
            header->setFlags(Qt::NoItemFlags);
            header->setForeground(theme::textDim());
        }

        auto* item = new QListWidgetItem(name, m_list);
        item->setData(Qt::UserRole, QString::fromLatin1(entry.type));
        item->setToolTip(tr("Double-click to apply to the selected clip"));
    }

    // Native plugin effects.
    const auto& plugins = PluginHost::instance().effects();
    if (!plugins.isEmpty()) {
        auto* header = new QListWidgetItem(tr("PLUGINS"), m_list);
        header->setFlags(Qt::NoItemFlags);
        header->setForeground(theme::textDim());
    }
    for (const auto& p : plugins) {
        if (!filter.isEmpty() && !p.name.contains(filter, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(p.name, p.category), m_list);
        item->setData(Qt::UserRole, p.type);
        item->setToolTip(tr("Plugin from %1").arg(p.library));
    }
}

void EffectsPanel::applyEffect(const QString& effectType)
{
    Sequence* seq = m_document->sequence();
    if (!seq) return;
    const QStringList selection = m_document->selectedClips();
    if (selection.isEmpty()) return;

    // Transitions go on both ends of every selected clip of the right kind.
    if (effectType.startsWith(QLatin1String("transition:")) || effectType.startsWith(QLatin1String("audiotransition:"))) {
        const bool audio = effectType.startsWith(QLatin1String("audio"));
        const auto type = static_cast<TransitionType>(effectType.section(QLatin1Char(':'), 1).toInt());
        const Tick duration = seq->rate.ticksForFrames(static_cast<qint64>(std::lround(seq->rate.value())));
        m_document->applyEdit(tr("Apply Transition"), [&](Sequence& s) {
            return edit::applyDefaultTransition(s, 0, selection, audio ? TrackKind::Audio : TrackKind::Video,
                                                type, duration);
        });
        return;
    }

    EffectInstance effect;
    if (!fx::makeByType(effectType, effect)) return;
    const bool audioEffect = effectType == QLatin1String(fx::kVolume);

    m_document->applyEdit(tr("Apply %1").arg(effect.name), [&](Sequence& s) {
        bool changed = false;
        for (const QString& id : selection) {
            int ti = -1;
            Clip* c = s.findClip(id, &ti);
            if (!c || s.tracks[ti].locked) continue;
            if ((s.tracks[ti].kind == TrackKind::Audio) != audioEffect) continue;
            if (!audioEffect) fx::ensureStandardStack(*c);
            bool exists = false;
            for (const auto& e : c->effects) exists = exists || e.type == effectType;
            if (exists) continue;
            EffectInstance copy = effect;
            copy.id = makeId(QStringLiteral("fx"));
            c->effects.push_back(copy);
            changed = true;
        }
        return changed;
    });
}

} // namespace bc
