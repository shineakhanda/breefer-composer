#include "ui/Dialogs.h"

#include "media/RemotionBridge.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace bc {
namespace {

void setButtonColor(QPushButton* button, const QColor& color)
{
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid #555; min-width: 48px; }")
                              .arg(color.name(QColor::HexArgb)));
}

QDialogButtonBox* okCancel(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

const struct RateChoice { const char* label; int num; int den; } kRates[] = {
    {"23.976 fps", 24000, 1001}, {"24 fps", 24, 1}, {"25 fps", 25, 1}, {"29.97 fps", 30000, 1001},
    {"30 fps", 30, 1}, {"50 fps", 50, 1}, {"59.94 fps", 60000, 1001}, {"60 fps", 60, 1},
};

} // namespace

// --------------------------------------------------------------- TitleDialog

TitleDialog::TitleDialog(const TitleSpec& spec, int seqWidth, int seqHeight, QWidget* parent)
    : QDialog(parent)
    , m_textColor(spec.color)
    , m_backgroundColor(spec.backgroundColor)
    , m_seqWidth(seqWidth)
    , m_seqHeight(seqHeight)
{
    setWindowTitle(tr("Essential Graphics - Title"));
    resize(860, 520);
    auto* root = new QHBoxLayout(this);

    auto* left = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(480, 270);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background: #000;"));
    left->addWidget(m_preview, 1);
    m_text = new QPlainTextEdit(spec.text, this);
    m_text->setMaximumHeight(90);
    left->addWidget(m_text);
    root->addLayout(left, 3);

    auto* form = new QFormLayout;
    m_font = new QFontComboBox(this);
    m_font->setCurrentFont(QFont(spec.fontFamily));
    form->addRow(tr("Font"), m_font);
    m_size = new QDoubleSpinBox(this);
    m_size->setRange(6, 600);
    m_size->setValue(spec.fontSize);
    form->addRow(tr("Size (px)"), m_size);
    auto* style = new QHBoxLayout;
    m_bold = new QCheckBox(tr("Bold"), this);
    m_bold->setChecked(spec.bold);
    m_italic = new QCheckBox(tr("Italic"), this);
    m_italic->setChecked(spec.italic);
    style->addWidget(m_bold);
    style->addWidget(m_italic);
    form->addRow(tr("Style"), style);
    m_color = new QPushButton(this);
    setButtonColor(m_color, m_textColor);
    form->addRow(tr("Fill"), m_color);
    m_align = new QComboBox(this);
    m_align->addItems({tr("Left"), tr("Center"), tr("Right")});
    m_align->setCurrentIndex(spec.align);
    form->addRow(tr("Align"), m_align);
    m_x = new QDoubleSpinBox(this);
    m_x->setRange(0, 100);
    m_x->setSuffix(QStringLiteral(" %"));
    m_x->setValue(spec.posX * 100.0);
    form->addRow(tr("Position X"), m_x);
    m_y = new QDoubleSpinBox(this);
    m_y->setRange(0, 100);
    m_y->setSuffix(QStringLiteral(" %"));
    m_y->setValue(spec.posY * 100.0);
    form->addRow(tr("Position Y"), m_y);
    m_outline = new QDoubleSpinBox(this);
    m_outline->setRange(0, 40);
    m_outline->setValue(spec.outline);
    form->addRow(tr("Stroke"), m_outline);
    m_shadow = new QCheckBox(tr("Shadow"), this);
    m_shadow->setChecked(spec.shadow);
    form->addRow(QString(), m_shadow);
    auto* bgRow = new QHBoxLayout;
    m_background = new QCheckBox(tr("Background"), this);
    m_background->setChecked(spec.background);
    m_bgColor = new QPushButton(this);
    setButtonColor(m_bgColor, m_backgroundColor);
    bgRow->addWidget(m_background);
    bgRow->addWidget(m_bgColor);
    form->addRow(QString(), bgRow);

    auto* presets = new QHBoxLayout;
    auto* lower = new QPushButton(tr("Lower third"), this);
    auto* centre = new QPushButton(tr("Centered"), this);
    presets->addWidget(lower);
    presets->addWidget(centre);
    form->addRow(tr("Layout"), presets);

    auto* right = new QVBoxLayout;
    right->addLayout(form);
    right->addStretch(1);
    right->addWidget(okCancel(this));
    root->addLayout(right, 2);

    connect(m_color, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_textColor, this, tr("Text color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) { m_textColor = c; setButtonColor(m_color, c); updatePreview(); }
    });
    connect(m_bgColor, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_backgroundColor, this, tr("Background"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) { m_backgroundColor = c; setButtonColor(m_bgColor, c); updatePreview(); }
    });
    connect(lower, &QPushButton::clicked, this, [this] {
        m_align->setCurrentIndex(0); m_x->setValue(8); m_y->setValue(82); m_size->setValue(m_seqHeight * 0.06);
        m_background->setChecked(true);
    });
    connect(centre, &QPushButton::clicked, this, [this] {
        m_align->setCurrentIndex(1); m_x->setValue(50); m_y->setValue(50); m_size->setValue(m_seqHeight * 0.1);
        m_background->setChecked(false);
    });

    connect(m_text, &QPlainTextEdit::textChanged, this, &TitleDialog::updatePreview);
    connect(m_font, &QFontComboBox::currentFontChanged, this, &TitleDialog::updatePreview);
    for (QDoubleSpinBox* s : {m_size, m_x, m_y, m_outline})
        connect(s, &QDoubleSpinBox::valueChanged, this, &TitleDialog::updatePreview);
    for (QCheckBox* c : {m_bold, m_italic, m_background, m_shadow})
        connect(c, &QCheckBox::toggled, this, &TitleDialog::updatePreview);
    connect(m_align, &QComboBox::currentIndexChanged, this, &TitleDialog::updatePreview);
    updatePreview();
}

TitleSpec TitleDialog::spec() const
{
    TitleSpec t;
    t.text = m_text->toPlainText();
    t.fontFamily = m_font->currentFont().family();
    t.fontSize = m_size->value();
    t.bold = m_bold->isChecked();
    t.italic = m_italic->isChecked();
    t.color = m_textColor;
    t.align = m_align->currentIndex();
    t.posX = m_x->value() / 100.0;
    t.posY = m_y->value() / 100.0;
    t.background = m_background->isChecked();
    t.backgroundColor = m_backgroundColor;
    t.shadow = m_shadow->isChecked();
    t.outline = m_outline->value();
    return t;
}

void TitleDialog::updatePreview()
{
    const int w = 480;
    const int h = std::max(10, w * m_seqHeight / std::max(1, m_seqWidth));
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    // Checkerboard shows transparency.
    QPainter p(&img);
    for (int y = 0; y < h; y += 12)
        for (int x = 0; x < w; x += 12)
            p.fillRect(x, y, 12, 12, ((x + y) / 12) % 2 ? QColor(0x30, 0x30, 0x34) : QColor(0x26, 0x26, 0x2a));
    const TitleSpec t = spec();
    const double s = static_cast<double>(w) / std::max(1, m_seqWidth);
    QFont font(t.fontFamily);
    font.setPixelSize(std::max(4, static_cast<int>(t.fontSize * s)));
    font.setBold(t.bold);
    font.setItalic(t.italic);
    const QFontMetricsF fm(font);
    const QStringList lines = t.text.split(QLatin1Char('\n'));
    double bw = 0;
    for (const auto& l : lines) bw = std::max(bw, fm.horizontalAdvance(l));
    const double lh = fm.lineSpacing();
    const double bh = lh * lines.size();
    double left = t.posX * w - bw / 2;
    if (t.align == 0) left = t.posX * w;
    if (t.align == 2) left = t.posX * w - bw;
    const double top = t.posY * h - bh / 2;
    p.setRenderHint(QPainter::Antialiasing, true);
    if (t.background) {
        const double pad = lh * 0.25;
        p.setPen(Qt::NoPen);
        p.setBrush(t.backgroundColor);
        p.drawRoundedRect(QRectF(left - pad * 1.6, top - pad, bw + pad * 3.2, bh + pad * 2), pad * 0.8, pad * 0.8);
    }
    QPainterPath path;
    for (int i = 0; i < lines.size(); ++i) {
        const double lw = fm.horizontalAdvance(lines[i]);
        double x = left;
        if (t.align == 1) x = left + (bw - lw) / 2;
        if (t.align == 2) x = left + (bw - lw);
        path.addText(QPointF(x, top + fm.ascent() + lh * i), font, lines[i]);
    }
    if (t.shadow) {
        p.save();
        p.translate(2, 2);
        p.fillPath(path, QColor(0, 0, 0, 150));
        p.restore();
    }
    if (t.outline > 0) p.strokePath(path, QPen(t.outlineColor, t.outline * s * 2));
    p.fillPath(path, t.color);
    p.end();
    m_preview->setPixmap(QPixmap::fromImage(img));
}

// ------------------------------------------------------------ SequenceDialog

SequenceDialog::SequenceDialog(const QString& name, int width, int height, FrameRate rate, bool isNew, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(isNew ? tr("New Sequence") : tr("Sequence Settings"));
    setMinimumWidth(420);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    m_name = new QLineEdit(name, this);
    form->addRow(tr("Sequence name"), m_name);

    m_preset = new QComboBox(this);
    struct P { const char* label; int w; int h; int num; int den; };
    static const P presets[] = {
        {"Custom", 0, 0, 0, 0},
        {"HD 1080p 25", 1920, 1080, 25, 1},
        {"HD 1080p 29.97", 1920, 1080, 30000, 1001},
        {"HD 1080p 23.976", 1920, 1080, 24000, 1001},
        {"HD 1080p 30", 1920, 1080, 30, 1},
        {"HD 1080p 60", 1920, 1080, 60, 1},
        {"HD 720p 30", 1280, 720, 30, 1},
        {"UHD 4K 2160p 25", 3840, 2160, 25, 1},
        {"UHD 4K 2160p 29.97", 3840, 2160, 30000, 1001},
        {"Vertical 9:16 1080x1920 30", 1080, 1920, 30, 1},
        {"Square 1:1 1080x1080 30", 1080, 1080, 30, 1},
        {"Portrait 4:5 1080x1350 30", 1080, 1350, 30, 1},
    };
    for (const auto& p : presets)
        m_preset->addItem(tr(p.label), QVariantList{p.w, p.h, p.num, p.den});
    form->addRow(tr("Preset"), m_preset);

    m_width = new QSpinBox(this);
    m_width->setRange(16, 8192);
    m_width->setSingleStep(2);
    m_width->setValue(width);
    m_height = new QSpinBox(this);
    m_height->setRange(16, 8192);
    m_height->setSingleStep(2);
    m_height->setValue(height);
    auto* size = new QHBoxLayout;
    size->addWidget(m_width);
    size->addWidget(new QLabel(QStringLiteral("x"), this));
    size->addWidget(m_height);
    form->addRow(tr("Frame size"), size);

    m_rate = new QComboBox(this);
    for (const auto& r : kRates) m_rate->addItem(QString::fromLatin1(r.label), QVariantList{r.num, r.den});
    for (int i = 0; i < m_rate->count(); ++i) {
        const QVariantList v = m_rate->itemData(i).toList();
        if (v[0].toInt() == rate.num && v[1].toInt() == rate.den) m_rate->setCurrentIndex(i);
    }
    form->addRow(tr("Timebase"), m_rate);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        const QVariantList v = m_preset->itemData(i).toList();
        if (v[0].toInt() <= 0) return;
        m_width->setValue(v[0].toInt());
        m_height->setValue(v[1].toInt());
        for (int k = 0; k < m_rate->count(); ++k) {
            const QVariantList r = m_rate->itemData(k).toList();
            if (r[0] == v[2] && r[1] == v[3]) m_rate->setCurrentIndex(k);
        }
    });

    layout->addLayout(form);
    if (!isNew) layout->addWidget(new QLabel(tr("Changing the timebase keeps clip positions in real time."), this));
    layout->addWidget(okCancel(this));
}

QString SequenceDialog::name() const { return m_name->text().trimmed().isEmpty() ? tr("Sequence") : m_name->text().trimmed(); }
int SequenceDialog::sequenceWidth() const { return m_width->value() & ~1; }
int SequenceDialog::sequenceHeight() const { return m_height->value() & ~1; }
FrameRate SequenceDialog::rate() const
{
    const QVariantList v = m_rate->currentData().toList();
    return FrameRate{v[0].toInt(), v[1].toInt()};
}

// -------------------------------------------------------------- ExportDialog

ExportDialog::ExportDialog(const Sequence& seq, const QString& baseName, const QString& lastFolder, QWidget* parent)
    : QDialog(parent)
    , m_seq(seq)
{
    setWindowTitle(tr("Export Settings"));
    setMinimumWidth(560);
    m_presets = Exporter::presets(seq.width, seq.height);
    const QStringList available = Exporter::availableVideoCodecs();

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    m_preset = new QComboBox(this);
    QString lastCat;
    for (int i = 0; i < m_presets.size(); ++i) {
        m_preset->addItem(QStringLiteral("%1  -  %2").arg(m_presets[i].category, m_presets[i].name), i);
    }
    form->addRow(tr("Preset"), m_preset);

    m_container = new QComboBox(this);
    for (const char* c : {"mp4", "mov", "mkv", "webm", "wav", "m4a"}) m_container->addItem(QString::fromLatin1(c));
    form->addRow(tr("Format"), m_container);

    m_codec = new QComboBox(this);
    const struct { const char* id; const char* label; } codecs[] = {
        {"h264", "H.264 / AVC"}, {"hevc", "H.265 / HEVC"}, {"prores", "Apple ProRes 422"},
        {"dnxhr", "Avid DNxHR HQ"}, {"vp9", "VP9"}, {"av1", "AV1"}, {"none", "No video (audio only)"}};
    for (const auto& c : codecs) {
        const QString id = QString::fromLatin1(c.id);
        if (id != QLatin1String("none") && !available.contains(id)) continue;
        m_codec->addItem(tr(c.label), id);
    }
    form->addRow(tr("Video codec"), m_codec);

    auto* size = new QHBoxLayout;
    m_width = new QSpinBox(this);
    m_width->setRange(16, 8192);
    m_width->setSingleStep(2);
    m_height = new QSpinBox(this);
    m_height->setRange(16, 8192);
    m_height->setSingleStep(2);
    size->addWidget(m_width);
    size->addWidget(new QLabel(QStringLiteral("x"), this));
    size->addWidget(m_height);
    auto* match = new QPushButton(tr("Match Sequence"), this);
    size->addWidget(match);
    form->addRow(tr("Frame size"), size);
    connect(match, &QPushButton::clicked, this, [this] {
        m_width->setValue(m_seq.width);
        m_height->setValue(m_seq.height);
    });

    m_bitrate = new QSpinBox(this);
    m_bitrate->setRange(500, 400000);
    m_bitrate->setSingleStep(1000);
    m_bitrate->setSuffix(QStringLiteral(" kbps"));
    form->addRow(tr("Target bitrate"), m_bitrate);

    m_hardware = new QCheckBox(tr("Hardware encoding when available (NVENC / Quick Sync / AMF)"), this);
    m_hardware->setChecked(true);
    form->addRow(QString(), m_hardware);

    m_audio = new QCheckBox(tr("Export audio"), this);
    m_audio->setChecked(true);
    form->addRow(QString(), m_audio);
    m_audioCodec = new QComboBox(this);
    m_audioCodec->addItem(tr("AAC"), QStringLiteral("aac"));
    m_audioCodec->addItem(tr("PCM 16-bit (uncompressed)"), QStringLiteral("pcm"));
    m_audioCodec->addItem(tr("Opus"), QStringLiteral("opus"));
    form->addRow(tr("Audio codec"), m_audioCodec);
    m_audioBitrate = new QSpinBox(this);
    m_audioBitrate->setRange(64, 512);
    m_audioBitrate->setSuffix(QStringLiteral(" kbps"));
    form->addRow(tr("Audio bitrate"), m_audioBitrate);

    m_range = new QComboBox(this);
    m_range->addItem(tr("Entire Sequence"));
    if (seq.hasRange() || seq.inPoint >= 0 || seq.outPoint >= 0) {
        m_range->addItem(tr("Sequence In/Out"));
        m_range->setCurrentIndex(1);
    }
    form->addRow(tr("Range"), m_range);

    auto* pathRow = new QHBoxLayout;
    const QString folder = lastFolder.isEmpty() ? QDir::homePath() : lastFolder;
    m_path = new QLineEdit(QDir(folder).filePath(baseName + QStringLiteral(".mp4")), this);
    auto* browse = new QPushButton(tr("Browse..."), this);
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);
    form->addRow(tr("Output"), pathRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString ext = m_container->currentText();
        const QString path = QFileDialog::getSaveFileName(this, tr("Export To"), m_path->text(),
                                                          tr("%1 files (*.%2)").arg(ext.toUpper(), ext));
        if (!path.isEmpty()) m_path->setText(path);
        updatePathSuffix();
    });

    layout->addLayout(form);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("color: #9a9aa4; padding: 6px; background: #1c1c1f; border-radius: 4px;"));
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* exportNow = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    QPushButton* queue = buttons->addButton(tr("Queue"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    exportNow->setDefault(true);
    connect(exportNow, &QPushButton::clicked, this, [this] { m_queueOnly = false; accept(); });
    connect(queue, &QPushButton::clicked, this, [this] { m_queueOnly = true; accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_preset, &QComboBox::currentIndexChanged, this, &ExportDialog::applyPreset);
    connect(m_container, &QComboBox::currentTextChanged, this, [this] { updatePathSuffix(); updateSummary(); });
    for (QSpinBox* s : {m_width, m_height, m_bitrate, m_audioBitrate})
        connect(s, &QSpinBox::valueChanged, this, &ExportDialog::updateSummary);
    connect(m_codec, &QComboBox::currentIndexChanged, this, &ExportDialog::updateSummary);
    connect(m_range, &QComboBox::currentIndexChanged, this, &ExportDialog::updateSummary);
    applyPreset(0);
}

void ExportDialog::applyPreset(int index)
{
    if (index < 0 || index >= m_presets.size()) return;
    const Exporter::Settings& s = m_presets[index].settings;
    m_container->setCurrentText(s.container);
    const int codec = m_codec->findData(s.videoCodec);
    if (codec >= 0) m_codec->setCurrentIndex(codec);
    m_width->setValue(s.width);
    m_height->setValue(s.height);
    m_bitrate->setValue(s.bitrateKbps);
    m_audioBitrate->setValue(s.audioBitrateKbps);
    const int ac = m_audioCodec->findData(s.audioCodec);
    if (ac >= 0) m_audioCodec->setCurrentIndex(ac);
    updatePathSuffix();
    updateSummary();
}

void ExportDialog::updatePathSuffix()
{
    QFileInfo info(m_path->text());
    const QString wanted = m_container->currentText();
    if (info.suffix().compare(wanted, Qt::CaseInsensitive) != 0)
        m_path->setText(QDir(info.absolutePath()).filePath(info.completeBaseName() + QLatin1Char('.') + wanted));
}

void ExportDialog::updateSummary()
{
    Tick from = 0;
    Tick to = m_seq.duration;
    if (m_range->currentIndex() == 1) {
        from = std::max<Tick>(0, m_seq.inPoint);
        to = m_seq.outPoint >= 0 ? m_seq.outPoint : m_seq.duration;
    }
    const double seconds = ticksToSec(std::max<Tick>(0, to - from));
    const bool video = m_codec->currentData().toString() != QLatin1String("none");
    double mb = (m_audio->isChecked() ? m_audioBitrate->value() : 0) * seconds / 8000.0;
    if (video) mb += m_bitrate->value() * seconds / 8000.0;
    m_summary->setText(tr("Source: %1, %2x%3, %4 fps\nOutput: %5 %6x%7 - %8 - ~%9 MB")
                           .arg(m_seq.name).arg(m_seq.width).arg(m_seq.height).arg(m_seq.rate.label())
                           .arg(m_codec->currentText()).arg(m_width->value()).arg(m_height->value())
                           .arg(formatTimecode(std::max<Tick>(0, to - from), m_seq.rate))
                           .arg(mb, 0, 'f', 1));
}

Exporter::Settings ExportDialog::settings() const
{
    Exporter::Settings s;
    s.outputPath = m_path->text();
    s.container = m_container->currentText();
    s.videoCodec = m_codec->currentData().toString();
    s.audioCodec = m_audioCodec->currentData().toString();
    s.width = m_width->value();
    s.height = m_height->value();
    s.bitrateKbps = m_bitrate->value();
    s.audioBitrateKbps = m_audioBitrate->value();
    s.includeAudio = m_audio->isChecked();
    s.preferHardware = m_hardware->isChecked();
    if (m_range->currentIndex() == 1) {
        s.rangeStart = std::max<Tick>(0, m_seq.inPoint);
        s.rangeEnd = m_seq.outPoint;
    }
    s.label = QStringLiteral("%1 -> %2").arg(m_seq.name, QFileInfo(s.outputPath).fileName());
    return s;
}

// ------------------------------------------------------------ RemotionDialog

RemotionDialog::RemotionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Remotion / React Composition"));
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Use a Remotion project, a pre-built Remotion bundle, or a React plugin "
                                "(package.json with a \"breefer\" section). It renders with your props as "
                                "ProRes 4444 with alpha and lands on the timeline as a clip you can re-render."), this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: #9b9b9b;"));
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    auto* projectRow = new QHBoxLayout;
    QSettings settings(QStringLiteral("Breefer"), QStringLiteral("Composer"));
    m_project = new QLineEdit(settings.value(QStringLiteral("remotionProject")).toString(), this);
    auto* browse = new QPushButton(tr("Browse..."), this);
    projectRow->addWidget(m_project, 1);
    projectRow->addWidget(browse);
    form->addRow(tr("Project / bundle"), projectRow);
    m_entry = new QLineEdit(this);
    m_entry->setPlaceholderText(tr("auto (remotion.config) or e.g. src/index.ts"));
    form->addRow(tr("Entry point"), m_entry);
    auto* compRow = new QHBoxLayout;
    m_composition = new QComboBox(this);
    m_composition->setEditable(true);
    auto* list = new QPushButton(tr("List"), this);
    compRow->addWidget(m_composition, 1);
    compRow->addWidget(list);
    form->addRow(tr("Composition"), compRow);
    m_props = new QPlainTextEdit(QStringLiteral("{}"), this);
    m_props->setFont(theme::monoFont(9));
    m_props->setMaximumHeight(120);
    form->addRow(tr("Props (JSON)"), m_props);
    layout->addLayout(form);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #9b9b9b;"));
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* render = buttons->addButton(tr("Render && Import"), QDialogButtonBox::AcceptRole);
    QPushButton* studio = buttons->addButton(tr("Open Remotion Studio"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    render->setDefault(true);
    connect(render, &QPushButton::clicked, this, [this] {
        const QJsonParseError err = [&] { QJsonParseError e{}; QJsonDocument::fromJson(m_props->toPlainText().toUtf8(), &e); return e; }();
        if (!m_props->toPlainText().trimmed().isEmpty() && err.error != QJsonParseError::NoError) {
            m_status->setText(tr("Props are not valid JSON: %1").arg(err.errorString()));
            return;
        }
        if (composition().isEmpty()) {
            m_status->setText(tr("Choose a composition (press List)."));
            return;
        }
        QSettings(QStringLiteral("Breefer"), QStringLiteral("Composer")).setValue(QStringLiteral("remotionProject"), project());
        accept();
    });
    connect(studio, &QPushButton::clicked, this, [this] {
        m_studio = true;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Remotion project or bundle"), m_project->text());
        if (!dir.isEmpty()) {
            m_project->setText(dir);
            detect();
        }
    });
    connect(list, &QPushButton::clicked, this, &RemotionDialog::refreshCompositions);
    if (!m_project->text().isEmpty()) detect();
    if (RemotionBridge::npxExecutable().isEmpty())
        m_status->setText(tr("Node.js was not found on PATH. Install Node.js 18 or newer (nodejs.org) to render Remotion compositions."));
}

void RemotionDialog::detect()
{
    QString kind;
    if (!RemotionBridge::isRemotionFolder(project(), &kind)) {
        m_status->setText(tr("This folder has no Remotion dependency and is not a Remotion bundle."));
        return;
    }
    if (kind == QLatin1String("react-plugin")) {
        // React plugin manifest: { "breefer": { "composition", "entry", "defaultProps" } }
        QFile pkg(QDir(project()).filePath(QStringLiteral("package.json")));
        if (pkg.open(QIODevice::ReadOnly)) {
            const QJsonObject b = QJsonDocument::fromJson(pkg.readAll()).object().value(QStringLiteral("breefer")).toObject();
            if (b.contains(QStringLiteral("entry"))) m_entry->setText(b.value(QStringLiteral("entry")).toString());
            if (b.contains(QStringLiteral("composition"))) m_composition->setEditText(b.value(QStringLiteral("composition")).toString());
            if (b.contains(QStringLiteral("defaultProps")))
                m_props->setPlainText(QString::fromUtf8(QJsonDocument(b.value(QStringLiteral("defaultProps")).toObject()).toJson()));
        }
    }
    m_status->setText(kind == QLatin1String("bundle") ? tr("Pre-built Remotion bundle detected (React binary plugin).")
                      : kind == QLatin1String("react-plugin") ? tr("Breefer React plugin detected.")
                                                              : tr("Remotion project detected. Press List to load compositions."));
}

void RemotionDialog::refreshCompositions()
{
    m_status->setText(tr("Asking Remotion for compositions..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    RemotionBridge bridge;
    QString error;
    const QStringList ids = bridge.listCompositions(project(), entry(), &error);
    QApplication::restoreOverrideCursor();
    if (ids.isEmpty()) {
        m_status->setText(error.isEmpty() ? tr("No compositions found.") : error.left(600));
        return;
    }
    const QString keep = m_composition->currentText();
    m_composition->clear();
    m_composition->addItems(ids);
    if (ids.contains(keep)) m_composition->setCurrentText(keep);
    m_status->setText(tr("%n composition(s) found.", nullptr, ids.size()));
}

QString RemotionDialog::project() const { return m_project->text().trimmed(); }
QString RemotionDialog::entry() const { return m_entry->text().trimmed(); }
QString RemotionDialog::composition() const { return m_composition->currentText().trimmed(); }
QString RemotionDialog::props() const { return m_props->toPlainText().trimmed(); }

// ------------------------------------------------------------ ShortcutsDialog

ShortcutsDialog::ShortcutsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(560, 640);
    auto* layout = new QVBoxLayout(this);
    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    const QList<QPair<QString, QString>> rows = {
        {tr("Play / Stop"), "Space"}, {tr("Shuttle reverse / stop / forward"), "J / K / L"},
        {tr("Step frame back / forward"), "Left / Right"}, {tr("Step 5 frames"), "Shift+Left / Right"},
        {tr("Previous / next edit point"), "Up / Down"}, {tr("Go to start / end"), "Home / End"},
        {tr("Mark In / Out"), "I / O"}, {tr("Mark Clip"), "X"}, {tr("Go to In / Out"), "Shift+I / Shift+O"},
        {tr("Clear In and Out"), "Ctrl+Shift+X"}, {tr("Insert / Overwrite from Source"), ", / ."},
        {tr("Lift / Extract"), "; / '"}, {tr("Add Edit (split)"), "Ctrl+K"},
        {tr("Add Edit to All Tracks"), "Ctrl+Shift+K"}, {tr("Ripple Trim Previous / Next Edit"), "Q / W"},
        {tr("Clear / Ripple Delete"), "Delete / Shift+Delete"}, {tr("Nudge clip 1 frame"), "Alt+Left / Right"},
        {tr("Nudge clip up / down a track"), "Alt+Up / Down"}, {tr("Apply Video Transition"), "Ctrl+D"},
        {tr("Apply Audio Transition"), "Ctrl+Shift+D"}, {tr("Speed/Duration"), "Ctrl+R"},
        {tr("Enable / disable clip"), "Shift+E"}, {tr("Link / Unlink"), "Ctrl+L"}, {tr("Match Frame"), "F"},
        {tr("Add Marker"), "M"}, {tr("Next / previous marker"), "Shift+M / Ctrl+Shift+M"},
        {tr("Selection tool"), "V"}, {tr("Track Select Forward tool"), "A"}, {tr("Ripple Edit tool"), "B"},
        {tr("Rolling Edit tool"), "N"}, {tr("Rate Stretch tool"), "R"}, {tr("Razor tool"), "C"},
        {tr("Slip / Slide tool"), "Y / U"}, {tr("Hand / Zoom tool"), "H / Z"},
        {tr("Snapping"), "S"}, {tr("Linked selection"), "Ctrl+Shift+L"}, {tr("Zoom in / out / fit"), "= / - / \\"},
        {tr("Copy / Cut / Paste / Paste Insert"), "Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+Shift+V"},
        {tr("Select All / Deselect All"), "Ctrl+A / Ctrl+Shift+A"}, {tr("Undo / Redo"), "Ctrl+Z / Ctrl+Shift+Z"},
        {tr("Export Media"), "Ctrl+M"}, {tr("Export Frame"), "Ctrl+Shift+E"}, {tr("Import"), "Ctrl+I"},
        {tr("Maximize panel under cursor"), "`"}, {tr("Save / Save As"), "Ctrl+S / Ctrl+Shift+S"},
        {tr("Timeline: Ctrl+drag edge"), tr("Ripple trim")}, {tr("Timeline: Ctrl+drag clip"), tr("Insert move")},
        {tr("Timeline: Alt+click clip"), tr("Select one side of a linked clip")},
        {tr("Timeline: drag yellow line on audio"), tr("Clip gain")},
    };
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(rows[i].first));
        table->setItem(i, 1, new QTableWidgetItem(rows[i].second));
    }
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace bc
