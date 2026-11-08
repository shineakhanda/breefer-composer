#include "ui/EffectControls.h"

#include "core/Document.h"
#include "engine/Effects.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <cmath>

namespace bc {

// ------------------------------------------------------------ KeyframeLane

KeyframeLane::KeyframeLane(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(20);
    setMinimumWidth(60);
}

void KeyframeLane::setData(Tick clipDuration, Tick playheadLocal, const QVector<Keyframe>& keys)
{
    m_duration = clipDuration;
    m_playhead = playheadLocal;
    m_keys = keys;
    update();
}

void KeyframeLane::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x19, 0x19, 0x19));
    if (m_duration <= 0) return;
    const double w = width() - 10;
    auto xOf = [&](Tick t) { return 5 + w * static_cast<double>(t) / static_cast<double>(m_duration); };
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& k : m_keys) {
        const double x = xOf(k.time);
        const bool atPlayhead = std::llabs(k.time - m_playhead) < m_duration / std::max(1.0, w);
        icons::paint(p, icons::Id::Keyframe, QRectF(x - 5, 5, 10, 10),
                     atPlayhead ? theme::hotText() : QColor(0xc8, 0xc8, 0xc8));
    }
    if (m_playhead >= 0 && m_playhead <= m_duration) {
        p.setPen(QPen(theme::playhead(), 1));
        const double x = xOf(m_playhead);
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
}

void KeyframeLane::mousePressEvent(QMouseEvent* event)
{
    if (m_duration <= 0) return;
    const double w = width() - 10;
    const double f = std::clamp((event->position().x() - 5) / w, 0.0, 1.0);
    Tick target = static_cast<Tick>(f * static_cast<double>(m_duration));
    // Snap to a nearby key.
    for (const auto& k : m_keys) {
        const double kx = 5 + w * static_cast<double>(k.time) / static_cast<double>(m_duration);
        if (std::abs(kx - event->position().x()) < 6) target = k.time;
    }
    emit seekRequested(target);
}

// ---------------------------------------------------------- EffectControls

EffectControls::EffectControls(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_header = new QLabel(this);
    m_header->setStyleSheet(QStringLiteral("color: #9b9b9b; padding: 5px 8px; border-bottom: 1px solid #262626;"));
    root->addWidget(m_header);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_content = new QWidget(m_scroll);
    m_layout = new QVBoxLayout(m_content);
    m_layout->setContentsMargins(0, 2, 0, 6);
    m_layout->setSpacing(0);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll, 1);

    connect(m_document, &Document::selectionChanged, this, &EffectControls::refresh);
    connect(m_document, &Document::structureChanged, this, &EffectControls::refresh);
    connect(m_document, &Document::documentLoaded, this, &EffectControls::refresh);
    // Playhead moves only update values in place (no widget rebuild).
    connect(m_document, &Document::playheadChanged, this, [this](Tick) {
        if (!m_live && isVisible()) updateValues();
    });
    refresh();
}

void EffectControls::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const bool lanes = width() >= 420;
    if (lanes != m_showLanes) {
        m_showLanes = lanes;
        for (auto& r : m_rows)
            if (r.lane) r.lane->setVisible(lanes);
    }
}

void EffectControls::refresh()
{
    if (m_live) return;
    rebuild();
}

Clip* EffectControls::liveClip()
{
    Sequence* seq = m_document->sequence();
    return seq ? seq->findClip(m_clipId) : nullptr;
}

Param* EffectControls::liveParam(const QString& effectId, const QString& paramId)
{
    Clip* c = liveClip();
    if (!c) return nullptr;
    for (auto& e : c->effects)
        if (e.id == effectId) return e.param(paramId);
    return nullptr;
}

void EffectControls::beginLive()
{
    if (m_live) return;
    m_live = true;
    m_before = m_document->snapshotSequence();
}

void EffectControls::endLive(const QString& text)
{
    if (!m_live) return;
    m_live = false;
    m_document->commitSequence(m_before, text);
    m_before = Sequence{};
}

void EffectControls::editParam(const QString& effectId, const QString& paramId,
                               const std::function<void(Param&, Tick)>& fn, const QString& text)
{
    const Tick playhead = m_document->playhead();
    m_document->applyEdit(text, [&](Sequence& s) {
        Clip* c = s.findClip(m_clipId);
        if (!c) return false;
        for (auto& e : c->effects) {
            if (e.id != effectId) continue;
            if (Param* p = e.param(paramId)) {
                fn(*p, std::clamp<Tick>(playhead - c->start, 0, c->duration));
                return true;
            }
        }
        return false;
    });
}

void EffectControls::editClip(const std::function<bool(Clip&)>& fn, const QString& text)
{
    m_document->applyEdit(text, [&](Sequence& s) {
        Clip* c = s.findClip(m_clipId);
        return c && fn(*c);
    });
}

void EffectControls::updateValues()
{
    const Sequence* seq = m_document->sequence();
    const Clip* clip = seq ? seq->findClip(m_clipId) : nullptr;
    if (!clip) return;
    const Tick local = m_document->playhead() - clip->start;
    for (auto& row : m_rows) {
        const Param* p = nullptr;
        for (const auto& e : clip->effects)
            if (e.id == row.effectId) p = e.param(row.paramId);
        if (!p) continue;
        if (row.value) row.value->setValue(p->evaluate(std::clamp<Tick>(local, 0, clip->duration)));
        if (row.lane) row.lane->setData(clip->duration, local, p->keys);
    }
}

void EffectControls::rebuild()
{
    m_rows.clear();
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    const Sequence* seq = m_document->sequence();
    m_clipId = m_document->selectedClipId();
    int trackIndex = -1;
    const Clip* clip = (seq && !m_clipId.isEmpty()) ? seq->findClip(m_clipId, &trackIndex) : nullptr;
    if (!clip) {
        m_header->setText(tr("(no clips selected)"));
        auto* hint = new QLabel(tr("Select a clip in the Timeline to adjust Motion,\nOpacity, Lumetri Color and other effects."), m_content);
        hint->setStyleSheet(QStringLiteral("color: #6b6b6b; padding: 16px;"));
        hint->setAlignment(Qt::AlignCenter);
        m_layout->addWidget(hint);
        m_layout->addStretch(1);
        return;
    }

    const bool audio = seq->tracks[trackIndex].kind == TrackKind::Audio;
    const MediaRef* media = m_document->project().mediaById(clip->mediaId);
    QString name = clip->label;
    if (name.isEmpty()) name = media ? media->name : (clip->generator == GeneratorKind::Title ? tr("Graphic") : tr("Color Matte"));
    m_header->setText(tr("Source - %1    %2 - %3").arg(name, seq->name, name));

    const Tick local = std::clamp<Tick>(m_document->playhead() - clip->start, 0, clip->duration);

    auto sectionLabel = [&](const QString& text) {
        auto* l = new QLabel(text, m_content);
        l->setStyleSheet(QStringLiteral("color: #e3e3e3; font-weight: 600; padding: 6px 8px 3px 8px; background: #222222;"));
        m_layout->addWidget(l);
    };
    sectionLabel(audio ? tr("Audio") : tr("Video"));

    if (clip->generator == GeneratorKind::Title) {
        auto* row = new QWidget(m_content);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(26, 4, 8, 4);
        auto* label = new QLabel(tr("Text: %1").arg(clip->title.text.section(QLatin1Char('\n'), 0, 0)), row);
        label->setStyleSheet(QStringLiteral("color: #c8c8c8;"));
        h->addWidget(label, 1);
        auto* edit = new QPushButton(tr("Edit Graphic..."), row);
        const QString id = clip->id;
        connect(edit, &QPushButton::clicked, this, [this, id] { emit editTitleRequested(id); });
        h->addWidget(edit);
        m_layout->addWidget(row);
    }

    if (audio) {
        // Clip gain lives on the clip itself, not in an effect.
        auto* row = new QWidget(m_content);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(26, 2, 8, 2);
        auto* label = new QLabel(tr("Clip Gain"), row);
        label->setFixedWidth(110);
        label->setStyleSheet(QStringLiteral("color: #c8c8c8;"));
        h->addWidget(label);
        auto* value = new ScrubValue(row);
        value->setRange(-60, 24);
        value->setSuffix(QStringLiteral(" dB"));
        value->setStepPerPixel(0.1);
        value->setValue(clip->gainDb);
        h->addWidget(value);
        h->addStretch(1);
        connect(value, &ScrubValue::editStarted, this, [this] { beginLive(); });
        connect(value, &ScrubValue::valueEdited, this, [this](double v) {
            if (Clip* c = liveClip()) { c->gainDb = v; m_document->setLiveSequence(*m_document->sequence()); }
        });
        connect(value, &ScrubValue::editFinished, this, [this](double v) {
            if (Clip* c = liveClip()) c->gainDb = v;
            endLive(tr("Clip Gain"));
        });
        m_layout->addWidget(row);
    }

    for (const auto& effect : clip->effects) {
        const QString effectId = effect.id;
        const bool collapsed = m_collapsed.contains(effectId);
        const bool intrinsic = effect.type == QLatin1String(fx::kMotion) || effect.type == QLatin1String(fx::kOpacity)
                               || effect.type == QLatin1String(fx::kVolume);

        // Effect header: twirl, fx bypass, name, reset.
        auto* header = new QWidget(m_content);
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        auto* hh = new QHBoxLayout(header);
        hh->setContentsMargins(4, 2, 6, 2);
        hh->setSpacing(2);
        auto* twirl = iconButton(collapsed ? icons::Id::TwirlClosed : icons::Id::TwirlOpen, tr("Show/hide"), header, false, 12);
        hh->addWidget(twirl);
        auto* fxButton = iconButton(effect.enabled ? icons::Id::FxOn : icons::Id::FxOff,
                                    tr("Toggle effect on/off (bypass)"), header, false, 16);
        hh->addWidget(fxButton);
        auto* title = new QLabel(effect.name, header);
        title->setStyleSheet(effect.enabled ? QStringLiteral("color: #e3e3e3; font-weight: 600;")
                                            : QStringLiteral("color: #6b6b6b; font-weight: 600;"));
        hh->addWidget(title, 1);
        auto* reset = iconButton(icons::Id::Reset, tr("Reset effect"), header, false, 14);
        hh->addWidget(reset);
        m_layout->addWidget(header);

        connect(twirl, &QToolButton::clicked, this, [this, effectId] {
            if (m_collapsed.contains(effectId)) m_collapsed.remove(effectId);
            else m_collapsed.insert(effectId);
            rebuild();
        });
        connect(fxButton, &QToolButton::clicked, this, [this, effectId] {
            editClip([&](Clip& c) {
                for (auto& e : c.effects)
                    if (e.id == effectId) { e.enabled = !e.enabled; return true; }
                return false;
            }, tr("Toggle Effect"));
        });
        connect(reset, &QToolButton::clicked, this, [this, effectId, type = effect.type] {
            EffectInstance fresh;
            if (!fx::makeByType(type, fresh)) return;
            editClip([&](Clip& c) {
                for (auto& e : c.effects)
                    if (e.id == effectId) {
                        fresh.id = e.id;
                        e = fresh;
                        return true;
                    }
                return false;
            }, tr("Reset Effect"));
        });
        connect(header, &QWidget::customContextMenuRequested, this, [this, header, effectId, intrinsic](const QPoint& pos) {
            QMenu menu(this);
            QAction* up = menu.addAction(tr("Move Up"));
            QAction* down = menu.addAction(tr("Move Down"));
            menu.addSeparator();
            QAction* del = menu.addAction(tr("Clear"));
            del->setEnabled(!intrinsic);
            QAction* chosen = menu.exec(header->mapToGlobal(pos));
            if (!chosen) return;
            editClip([&](Clip& c) {
                for (int i = 0; i < c.effects.size(); ++i) {
                    if (c.effects[i].id != effectId) continue;
                    if (chosen == del) { c.effects.removeAt(i); return true; }
                    const int j = chosen == up ? i - 1 : i + 1;
                    if (j < 0 || j >= c.effects.size()) return false;
                    c.effects.swapItemsAt(i, j);
                    return true;
                }
                return false;
            }, chosen == del ? tr("Clear Effect") : tr("Reorder Effects"));
        });

        if (collapsed) continue;

        for (const auto& param : effect.params) {
            const QString paramId = param.id;
            const bool animated = !param.keys.isEmpty();

            auto* row = new QWidget(m_content);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(22, 0, 6, 0);
            h->setSpacing(4);

            auto* watch = iconButton(animated ? icons::Id::StopwatchOn : icons::Id::Stopwatch,
                                     tr("Toggle animation"), row, true, 14);
            watch->setChecked(animated);
            h->addWidget(watch);

            auto* label = new QLabel(param.name, row);
            label->setFixedWidth(104);
            label->setStyleSheet(QStringLiteral("color: #c8c8c8;"));
            h->addWidget(label);

            auto* value = new ScrubValue(row);
            value->setRange(param.minValue, param.maxValue);
            const double span = param.maxValue - param.minValue;
            value->setStepPerPixel(span > 500 ? 1.0 : span > 50 ? 0.5 : span > 5 ? 0.1 : 0.01);
            value->setDecimals(span > 5 ? 1 : 2);
            value->setValue(param.evaluate(local));
            h->addWidget(value);

            auto* nav = new QWidget(row);
            auto* nh = new QHBoxLayout(nav);
            nh->setContentsMargins(0, 0, 0, 0);
            nh->setSpacing(0);
            auto* prev = iconButton(icons::Id::PrevKeyframe, tr("Go to Previous Keyframe"), nav, false, 10);
            auto* add = iconButton(icons::Id::Keyframe, tr("Add/Remove Keyframe"), nav, false, 11);
            auto* next = iconButton(icons::Id::NextKeyframe, tr("Go to Next Keyframe"), nav, false, 10);
            nh->addWidget(prev);
            nh->addWidget(add);
            nh->addWidget(next);
            nav->setVisible(animated);
            h->addWidget(nav);
            h->addStretch(1);

            auto* resetParam = iconButton(icons::Id::Reset, tr("Reset Parameter"), row, false, 12);
            h->addWidget(resetParam);

            auto* lane = new KeyframeLane(row);
            lane->setData(clip->duration, local, param.keys);
            lane->setVisible(m_showLanes);
            h->addWidget(lane, 2);

            m_layout->addWidget(row);
            m_rows.push_back({effectId, paramId, value, lane, nav});

            const double defaultValue = [&] {
                EffectInstance fresh;
                if (fx::makeByType(effect.type, fresh))
                    if (const Param* fp = fresh.param(paramId)) return fp->value;
                return param.value;
            }();

            connect(value, &ScrubValue::editStarted, this, [this] { beginLive(); });
            connect(value, &ScrubValue::valueEdited, this, [this, effectId, paramId](double v) {
                Param* p = liveParam(effectId, paramId);
                Clip* c = liveClip();
                if (!p || !c) return;
                const Tick l = std::clamp<Tick>(m_document->playhead() - c->start, 0, c->duration);
                if (p->keys.isEmpty()) p->value = v;
                else p->setKey(l, v);
                m_document->setLiveSequence(*m_document->sequence());
            });
            connect(value, &ScrubValue::editFinished, this, [this, effectId, paramId, name = param.name](double v) {
                Param* p = liveParam(effectId, paramId);
                Clip* c = liveClip();
                if (p && c) {
                    const Tick l = std::clamp<Tick>(m_document->playhead() - c->start, 0, c->duration);
                    if (p->keys.isEmpty()) p->value = v;
                    else p->setKey(l, v);
                }
                endLive(tr("Adjust %1").arg(name));
                updateValues();
            });

            connect(watch, &QToolButton::clicked, this, [this, effectId, paramId, animated](bool) {
                if (animated && QMessageBox::question(this, tr("Turn off animation"),
                                                      tr("This will delete existing keyframes. Continue?")) != QMessageBox::Yes) {
                    rebuild();
                    return;
                }
                editParam(effectId, paramId, [animated](Param& p, Tick l) {
                    if (animated) {
                        p.value = p.evaluate(l);
                        p.keys.clear();
                    } else {
                        p.setKey(l, p.value);
                    }
                }, animated ? tr("Disable Animation") : tr("Enable Animation"));
            });
            connect(add, &QToolButton::clicked, this, [this, effectId, paramId] {
                editParam(effectId, paramId, [](Param& p, Tick l) {
                    if (!p.removeKey(l)) p.setKey(l, p.evaluate(l));
                }, tr("Add/Remove Keyframe"));
            });
            auto jump = [this, effectId, paramId](bool forward) {
                const Sequence* s = m_document->sequence();
                const Clip* c = s ? s->findClip(m_clipId) : nullptr;
                if (!c) return;
                const Param* p = nullptr;
                for (const auto& e : c->effects)
                    if (e.id == effectId) p = e.param(paramId);
                if (!p) return;
                const Tick l = m_document->playhead() - c->start;
                if (forward) {
                    for (const auto& k : p->keys)
                        if (k.time > l) { m_document->setPlayhead(c->start + k.time); return; }
                } else {
                    for (int i = p->keys.size() - 1; i >= 0; --i)
                        if (p->keys[i].time < l) { m_document->setPlayhead(c->start + p->keys[i].time); return; }
                }
            };
            connect(prev, &QToolButton::clicked, this, [jump] { jump(false); });
            connect(next, &QToolButton::clicked, this, [jump] { jump(true); });
            connect(resetParam, &QToolButton::clicked, this, [this, effectId, paramId, defaultValue] {
                editParam(effectId, paramId, [defaultValue](Param& p, Tick l) {
                    if (p.keys.isEmpty()) p.value = defaultValue;
                    else p.setKey(l, defaultValue);
                }, tr("Reset Parameter"));
            });
            connect(lane, &KeyframeLane::seekRequested, this, [this](Tick l) {
                const Sequence* s = m_document->sequence();
                if (const Clip* c = s ? s->findClip(m_clipId) : nullptr) m_document->setPlayhead(c->start + l);
            });
        }
    }
    m_layout->addStretch(1);
}

} // namespace bc
