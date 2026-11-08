#include "ui/ColorPanels.h"

#include "core/Document.h"
#include "engine/Effects.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QFileDialog>
#include <QMessageBox>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <vector>

namespace bc {
namespace {

QLabel* sectionLabel(const QString& text, QWidget* parent = nullptr)
{
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral(
        "color: #a8a8b0; font-weight: 600; padding: 6px 0 2px 0;"));
    return label;
}

} // namespace

LumetriPanel::LumetriPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_content = new QWidget(m_scroll);
    m_layout = new QVBoxLayout(m_content);
    m_layout->setContentsMargins(10, 8, 10, 10);
    m_layout->setSpacing(4);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll);

    connect(m_document, &Document::selectionChanged, this, &LumetriPanel::refresh);
    connect(m_document, &Document::structureChanged, this, &LumetriPanel::refresh);
    refresh();
}

void LumetriPanel::refresh()
{
    if (m_live) return;

    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    m_clipId = m_document->selectedClipId();
    m_trackIndex = -1;

    const Sequence* seq = m_document->sequence();
    const Clip* clip = nullptr;
    if (!m_clipId.isEmpty() && seq)
        clip = seq->findClip(m_clipId, &m_trackIndex);

    if (!clip) {
        auto* hint = new QLabel(tr("Select a video clip to grade.\n\n"
                                   "Basic Correction, Creative looks and\n"
                                   "keyframes are written to the clip."), m_content);
        hint->setStyleSheet(QStringLiteral("color: #8a8a92;"));
        m_layout->addWidget(hint);
        m_layout->addStretch(1);
        return;
    }

    const Track* track = seq->track(m_trackIndex);
    if (track && track->kind == TrackKind::Audio) {
        auto* hint = new QLabel(tr("Lumetri Color applies to video clips."), m_content);
        hint->setStyleSheet(QStringLiteral("color: #8a8a92;"));
        m_layout->addWidget(hint);
        m_layout->addStretch(1);
        return;
    }

    m_layout->addWidget(sectionLabel(tr("Basic Correction"), m_content));

    auto* lookRow = new QWidget(m_content);
    auto* lookLayout = new QHBoxLayout(lookRow);
    lookLayout->setContentsMargins(0, 0, 0, 0);
    lookLayout->addWidget(new QLabel(tr("Look"), lookRow));
    auto* look = new QComboBox(lookRow);
    look->addItems({tr("None"), tr("Cinematic"), tr("Warm Film"), tr("Cool Noir"),
                    tr("Black & White")});
    lookLayout->addWidget(look, 1);
    auto* intensity = new QDoubleSpinBox(lookRow);
    intensity->setRange(0, 100);
    intensity->setValue(100);
    intensity->setSuffix(QStringLiteral(" %"));
    intensity->setFixedWidth(64);
    lookLayout->addWidget(intensity);
    m_layout->addWidget(lookRow);

    auto* cubeRow = new QWidget(m_content);
    auto* cubeLayout = new QHBoxLayout(cubeRow);
    cubeLayout->setContentsMargins(0, 2, 0, 2);
    cubeLayout->addStretch(1);
    auto* exportCube = new QPushButton(tr("Export .cube"), cubeRow);
    exportCube->setToolTip(tr("Save this grade as a 3D LUT for other clips, projects or monitors"));
    cubeLayout->addWidget(exportCube);
    m_layout->addWidget(cubeRow);
    connect(exportCube, &QPushButton::clicked, this, [this] {
        const Sequence* s = m_document->sequence();
        const Clip* c = s ? s->findClip(m_clipId) : nullptr;
        if (!c) return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export .cube"), QStringLiteral("Lumetri.cube"),
                                                          tr("Cube LUT (*.cube)"));
        if (path.isEmpty()) return;
        QString error;
        const Tick local = std::clamp<Tick>(m_document->playhead() - c->start, 0, c->duration);
        if (!fx::exportCube(*c, local, path, 33, &error))
            QMessageBox::warning(this, tr("Export .cube"), error);
    });

    connect(look, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        struct Preset { double temp, contrast, saturation, blacks; };
        static const Preset presets[] = {
            {0, 0, 100, 0},
            {8, 24, 96, -6},
            {26, 10, 104, 4},
            {-24, 18, 88, -12},
            {0, 6, 0, 0},
        };
        const Preset& p = presets[std::clamp(index, 0, 4)];
        m_document->editTrack(m_trackIndex, tr("Lumetri Look"), [&](QVector<Clip>& clips) {
            for (auto& c : clips) {
                if (c.id != m_clipId) continue;
                fx::ensureStandardStack(c);
                if (auto* t = fx::findParam(c, fx::kLumetri, "temperature")) t->value = p.temp;
                if (auto* t = fx::findParam(c, fx::kLumetri, "contrast")) t->value = p.contrast;
                if (auto* t = fx::findParam(c, fx::kLumetri, "saturation")) t->value = p.saturation;
                if (auto* t = fx::findParam(c, fx::kLumetri, "blacks")) t->value = p.blacks;
            }
        });
    });

    m_layout->addWidget(sectionLabel(tr("White Balance"), m_content));
    addParamRow(m_layout, "temperature", tr("Temperature"), -100, 100, QString());
    addParamRow(m_layout, "tint", tr("Tint"), -100, 100, QString());

    m_layout->addWidget(sectionLabel(tr("Tone"), m_content));
    addParamRow(m_layout, "exposure", tr("Exposure"), -5, 5, QString());
    addParamRow(m_layout, "contrast", tr("Contrast"), -100, 100, QString());
    addParamRow(m_layout, "highlights", tr("Highlights"), -100, 100, QString());
    addParamRow(m_layout, "shadows", tr("Shadows"), -100, 100, QString());
    addParamRow(m_layout, "whites", tr("Whites"), -100, 100, QString());
    addParamRow(m_layout, "blacks", tr("Blacks"), -100, 100, QString());

    m_layout->addWidget(sectionLabel(tr("Creative"), m_content));
    addParamRow(m_layout, "saturation", tr("Saturation"), 0, 200, QString());
    addParamRow(m_layout, "vibrance", tr("Vibrance"), -100, 100, QString());

    m_layout->addStretch(1);
}

void LumetriPanel::addParamRow(QVBoxLayout* layout, const char* paramId,
                               const QString& label, double minimum, double maximum,
                               const QString& suffix)
{
    const Sequence* seq = m_document->sequence();
    const Clip* clip = seq ? seq->findClip(m_clipId) : nullptr;
    if (!clip) return;

    const Param* existing = fx::findParam(*clip, fx::kLumetri, paramId);
    if (!existing) return;

    const Tick local = std::max<Tick>(0, m_document->playhead() - clip->start);
    const double current = existing->evaluate(local);

    auto* row = new QWidget(m_content);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);

    auto* name = new QLabel(label, row);
    name->setFixedWidth(86);
    name->setStyleSheet(QStringLiteral("color: #c0c0c8;"));
    rowLayout->addWidget(name);

    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(static_cast<int>(minimum * 10), static_cast<int>(maximum * 10));
    slider->setValue(static_cast<int>(current * 10));
    rowLayout->addWidget(slider, 1);

    auto* spin = new QDoubleSpinBox(row);
    spin->setRange(minimum, maximum);
    spin->setDecimals(1);
    spin->setValue(current);
    spin->setSuffix(suffix);
    spin->setFixedWidth(72);
    rowLayout->addWidget(spin);

    auto* keyButton = iconButton(icons::Id::Keyframe, tr("Add keyframe at playhead"), row, false, 12);
    rowLayout->addWidget(keyButton);

    layout->addWidget(row);

    connect(slider, &QSlider::sliderPressed, this, &LumetriPanel::beginLiveEdit);
    connect(slider, &QSlider::valueChanged, this, [this, spin, paramId](int value) {
        QSignalBlocker block(spin);
        spin->setValue(value / 10.0);
        applyLive(paramId, value / 10.0);
    });
    connect(slider, &QSlider::sliderReleased, this,
            [this] { commitLiveEdit(tr("Lumetri Color")); });

    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, slider, paramId](double value) {
                QSignalBlocker block(slider);
                slider->setValue(static_cast<int>(value * 10));
                m_document->editTrack(m_trackIndex, tr("Lumetri Color"), [&](QVector<Clip>& clips) {
                    for (auto& c : clips) {
                        if (c.id != m_clipId) continue;
                        fx::ensureStandardStack(c);
                        if (auto* p = fx::findParam(c, fx::kLumetri, paramId)) p->value = value;
                    }
                });
            });

    connect(keyButton, &QPushButton::clicked, this, [this, paramId, slider] {
        const double value = slider->value() / 10.0;
        m_document->editTrack(m_trackIndex, tr("Add Keyframe"), [&](QVector<Clip>& clips) {
            for (auto& c : clips) {
                if (c.id != m_clipId) continue;
                fx::ensureStandardStack(c);
                if (auto* p = fx::findParam(c, fx::kLumetri, paramId)) {
                    const Tick local = std::clamp<Tick>(m_document->playhead() - c.start,
                                                        0, c.duration);
                    p->setKey(local, value);
                }
            }
        });
    });
}

void LumetriPanel::beginLiveEdit()
{
    if (Sequence* seq = m_document->sequence()) {
        if (Track* track = seq->track(m_trackIndex)) {
            m_before = track->clips;
            m_live = true;
        }
    }
}

void LumetriPanel::applyLive(const char* paramId, double value)
{
    if (!m_live) {
        beginLiveEdit();
        if (!m_live) return;
    }
    Sequence* seq = m_document->sequence();
    if (!seq) return;
    Clip* clip = seq->findClip(m_clipId);
    if (!clip) return;

    fx::ensureStandardStack(*clip);
    if (Param* p = fx::findParam(*clip, fx::kLumetri, paramId)) {
        const Tick local = m_document->playhead() - clip->start;
        if (!p->keys.isEmpty() && local >= 0 && local < clip->duration)
            p->setKey(local, value);
        else
            p->value = value;
    }
    m_document->notifyChanged();
}

void LumetriPanel::commitLiveEdit(const QString& text)
{
    if (!m_live) return;
    m_live = false;
    Sequence* seq = m_document->sequence();
    if (!seq || m_trackIndex < 0) return;
    Track* track = seq->track(m_trackIndex);
    if (!track) return;
    m_document->commitTrackEdit(m_trackIndex, m_before, track->clips, text);
    m_before.clear();
}

// ---------------------------------------------------------------------- scopes

ScopesPanel::ScopesPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(220, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ScopesPanel::setImage(const QImage& image)
{
    m_image = image;
    // Scopes are a diagnostic, not a video path: refresh at ~7 Hz so the UI
    // thread always has room to repaint the actual monitor.
    if (!m_throttle.isValid() || m_throttle.elapsed() > 140) {
        m_throttle.restart();
        update();
    }
}

QImage ScopesPanel::drawWaveform(const QImage& source) const
{
    QImage out(256, 256, QImage::Format_ARGB32);
    out.fill(QColor(0x12, 0x12, 0x15));

    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    std::vector<int> histogram(256 * 256, 0);
    const int step = std::max(1, image.width() / 192);

    for (int y = 0; y < image.height(); y += step) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); x += step) {
            const QRgb px = line[x];
            const double luma = 0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px);
            const int bx = std::clamp(x * 255 / std::max(1, image.width()), 0, 255);
            const int by = std::clamp(255 - static_cast<int>(luma), 0, 255);
            ++histogram[by * 256 + bx];
        }
    }

    for (int y = 0; y < 256; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < 256; ++x) {
            const int count = histogram[y * 256 + x];
            if (!count) continue;
            const int v = std::min(255, 40 + count * 16);
            line[x] = qRgb(v * 4 / 10, v, v * 55 / 100);
        }
    }

    QPainter p(&out);
    p.setPen(QColor(0x44, 0x44, 0x4c));
    for (int i = 1; i < 4; ++i) {
        const int gy = 256 * i / 4;
        p.drawLine(0, gy, 256, gy);
    }
    return out;
}

QImage ScopesPanel::drawParade(const QImage& source) const
{
    QImage out(288, 256, QImage::Format_ARGB32);
    out.fill(QColor(0x12, 0x12, 0x15));

    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int step = std::max(1, image.width() / 96);
    std::vector<int> hist(3 * 96 * 256, 0);

    for (int y = 0; y < image.height(); y += step) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); x += step) {
            const QRgb px = line[x];
            const int bx = std::clamp(x * 95 / std::max(1, image.width()), 0, 95);
            const int channels[3] = {qRed(px), qGreen(px), qBlue(px)};
            for (int c = 0; c < 3; ++c) {
                const int by = std::clamp(255 - channels[c], 0, 255);
                ++hist[(c * 96 + bx) * 256 + by];
            }
        }
    }

    for (int y = 0; y < 256; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int c = 0; c < 3; ++c) {
            for (int x = 0; x < 96; ++x) {
                const int count = hist[(c * 96 + x) * 256 + y];
                if (!count) continue;
                const int v = std::min(255, 40 + count * 16);
                const int px = c * 96 + x;
                switch (c) {
                case 0: line[px] = qRgb(v, v / 5, v / 5); break;
                case 1: line[px] = qRgb(v / 5, v, v / 4); break;
                default: line[px] = qRgb(v / 4, v / 3, v); break;
                }
            }
        }
    }
    return out;
}

QImage ScopesPanel::drawVectorscope(const QImage& source) const
{
    QImage out(256, 256, QImage::Format_ARGB32);
    out.fill(QColor(0x12, 0x12, 0x15));

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QColor(0x3a, 0x3a, 0x42));
    p.drawEllipse(QPointF(128, 128), 96, 96);
    p.drawEllipse(QPointF(128, 128), 48, 48);
    p.drawLine(32, 128, 224, 128);
    p.drawLine(128, 32, 128, 224);

    // Skin tone line, ~33 degrees up-left from centre.
    p.setPen(QColor(0x6a, 0x5a, 0x3a));
    p.drawLine(128, 128, 128 - 66, 128 - 60);

    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int step = std::max(1, image.width() / 160);

    std::vector<int> hist(256 * 256, 0);
    for (int y = 0; y < image.height(); y += step) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); x += step) {
            const QRgb px = line[x];
            const double r = qRed(px) / 255.0;
            const double g = qGreen(px) / 255.0;
            const double b = qBlue(px) / 255.0;
            const double u = -0.169 * r - 0.331 * g + 0.500 * b;
            const double v = 0.500 * r - 0.419 * g - 0.081 * b;
            const int vx = std::clamp(128 + static_cast<int>(u * 220.0), 0, 255);
            const int vy = std::clamp(128 - static_cast<int>(v * 220.0), 0, 255);
            ++hist[vy * 256 + vx];
        }
    }

    p.end();

    for (int y = 0; y < 256; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < 256; ++x) {
            const int count = hist[y * 256 + x];
            if (!count) continue;
            const int v = std::min(255, 60 + count * 22);
            line[x] = qRgb(v, v, std::min(255, v + 12));
        }
    }
    return out;
}

void ScopesPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x12, 0x12, 0x15));

    if (m_image.isNull()) {
        p.setPen(theme::textDim());
        p.drawText(rect(), Qt::AlignCenter, tr("No signal"));
        return;
    }

    QImage scope = (m_mode == 0) ? drawWaveform(m_image)
                   : (m_mode == 1) ? drawParade(m_image)
                                   : drawVectorscope(m_image);

    const QSize target = scope.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect targetRect(QPoint((width() - target.width()) / 2,
                                  (height() - target.height()) / 2),
                           target);
    p.drawImage(targetRect, scope.scaled(target, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    p.setPen(theme::borderSoft());
    p.drawRect(targetRect.adjusted(-1, -1, 0, 0));
}

} // namespace bc
