#include "ui/MonitorWidget.h"

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace bc {

// ------------------------------------------------------------------ ScrubBar

ScrubBar::ScrubBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(16);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

int ScrubBar::xOf(Tick t) const
{
    const int w = std::max(1, width() - 16);
    if (m_duration <= 0) return 8;
    return 8 + static_cast<int>(w * static_cast<double>(t) / static_cast<double>(m_duration));
}

Tick ScrubBar::tickAt(int x) const
{
    const int w = std::max(1, width() - 16);
    const double f = std::clamp((x - 8) / static_cast<double>(w), 0.0, 1.0);
    return static_cast<Tick>(f * static_cast<double>(m_duration));
}

void ScrubBar::setPosition(Tick position)
{
    if (position == m_position) return;
    const int oldX = xOf(m_position);
    m_position = position;
    const int newX = xOf(m_position);
    // Repaint only the two playhead columns.
    update(QRect(std::min(oldX, newX) - 7, 0, std::abs(newX - oldX) + 14, height()));
}

void ScrubBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), theme::panelBg());
    const QRect track(8, height() - 5, width() - 16, 2);
    p.fillRect(track, QColor(0x3a, 0x3a, 0x3a));
    if (m_duration <= 0) return;
    // Ticks.
    p.setPen(QColor(0x4a, 0x4a, 0x4a));
    for (int i = 0; i <= 20; ++i) {
        const int x = 8 + (width() - 16) * i / 20;
        p.drawLine(x, height() - (i % 5 == 0 ? 9 : 7), x, height() - 5);
    }
    if (m_in >= 0 || m_out >= 0) {
        const int a = xOf(std::max<Tick>(0, m_in));
        const int b = xOf(m_out >= 0 ? m_out : m_duration);
        p.fillRect(QRect(a, height() - 7, std::max(1, b - a), 6), QColor(0x8a, 0x8a, 0x8a, 120));
    }
    for (Tick m : m_markers) {
        p.fillRect(QRect(xOf(m) - 2, 1, 5, 5), theme::labelColor(5));
    }
    const int x = xOf(m_position);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::playhead());
    QPolygon head;
    head << QPoint(x - 5, 0) << QPoint(x + 5, 0) << QPoint(x + 5, 5) << QPoint(x, 10) << QPoint(x - 5, 5);
    p.drawPolygon(head);
    p.fillRect(QRect(x, 8, 1, height() - 8), theme::playhead());
}

void ScrubBar::mousePressEvent(QMouseEvent* event)
{
    emit scrubStarted();
    emit scrubbed(tickAt(event->position().toPoint().x()));
}

void ScrubBar::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) emit scrubbed(tickAt(event->position().toPoint().x()));
}

// ------------------------------------------------------------- MonitorWidget

MonitorWidget::MonitorWidget(Role role, QWidget* parent)
    : QWidget(parent)
    , m_role(role)
{
    setMinimumSize(240, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    buildUi();
}

void MonitorWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch(1);

    m_controls = new QWidget(this);
    m_controls->setAutoFillBackground(true);
    auto* controls = new QVBoxLayout(m_controls);
    controls->setContentsMargins(0, 0, 0, 2);
    controls->setSpacing(0);

    // Timecode row: current | fit | resolution | wrench | duration.
    auto* tcRow = new QHBoxLayout;
    tcRow->setContentsMargins(8, 2, 8, 0);
    tcRow->setSpacing(6);
    m_timecode = new QLabel(QStringLiteral("00:00:00:00"), m_controls);
    m_timecode->setFont(theme::monoFont(10));
    m_timecode->setStyleSheet(QStringLiteral("color: #4ba3ff;"));
    tcRow->addWidget(m_timecode);
    tcRow->addStretch(1);

    m_zoom = new QComboBox(m_controls);
    m_zoom->setObjectName(QStringLiteral("Flat"));
    m_zoom->setToolTip(tr("Select Zoom Level"));
    m_zoom->addItem(tr("Fit"), 0.0);
    for (int z : {10, 25, 50, 75, 100, 150, 200}) m_zoom->addItem(QStringLiteral("%1%").arg(z), z / 100.0);
    connect(m_zoom, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_zoomFactor = m_zoom->itemData(i).toDouble();
        m_scaled = QImage();
        update();
    });
    tcRow->addWidget(m_zoom);

    m_resolution = new QComboBox(m_controls);
    m_resolution->setObjectName(QStringLiteral("Flat"));
    m_resolution->setToolTip(tr("Select Playback Resolution"));
    for (int d : {1, 2, 4, 8, 16}) m_resolution->addItem(d == 1 ? tr("Full") : QStringLiteral("1/%1").arg(d), d);
    m_resolution->setCurrentIndex(1);
    connect(m_resolution, &QComboBox::currentIndexChanged, this, [this](int i) {
        emit resolutionChanged(m_resolution->itemData(i).toInt());
    });
    tcRow->addWidget(m_resolution);

    m_wrench = iconButton(icons::Id::Wrench, tr("Settings"), m_controls, false, 14);
    m_wrench->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_wrench);
    m_safeAction = menu->addAction(tr("Safe Margins"));
    m_safeAction->setCheckable(true);
    connect(m_safeAction, &QAction::toggled, this, &MonitorWidget::setSafeMargins);
    QAction* loop = menu->addAction(tr("Loop"));
    loop->setCheckable(true);
    connect(loop, &QAction::toggled, this, &MonitorWidget::loopToggled);
    m_wrench->setMenu(menu);
    tcRow->addWidget(m_wrench);
    tcRow->addStretch(1);

    m_duration = new QLabel(QStringLiteral("00:00:00:00"), m_controls);
    m_duration->setFont(theme::monoFont(10));
    m_duration->setStyleSheet(QStringLiteral("color: #c8c8c8;"));
    tcRow->addWidget(m_duration);
    controls->addLayout(tcRow);

    m_scrub = new ScrubBar(m_controls);
    connect(m_scrub, &ScrubBar::scrubbed, this, &MonitorWidget::scrubbed);
    connect(m_scrub, &ScrubBar::scrubStarted, this, &MonitorWidget::scrubStarted);
    controls->addWidget(m_scrub);

    // Transport row, centred like Premiere's button bar.
    auto* row = new QHBoxLayout;
    row->setContentsMargins(4, 2, 4, 0);
    row->setSpacing(0);
    row->addStretch(1);
    auto add = [&](icons::Id id, const QString& tip, bool secondary = false) {
        auto* b = iconButton(id, tip, m_controls, false, 16);
        row->addWidget(b);
        if (secondary) m_secondary << b;
        return b;
    };
    auto gap = [&] { row->addSpacing(8); };

    connect(add(icons::Id::AddMarker, tr("Add Marker (M)"), true), &QToolButton::clicked, this, &MonitorWidget::addMarkerRequested);
    connect(add(icons::Id::MarkIn, tr("Mark In (I)")), &QToolButton::clicked, this, &MonitorWidget::setInRequested);
    connect(add(icons::Id::MarkOut, tr("Mark Out (O)")), &QToolButton::clicked, this, &MonitorWidget::setOutRequested);
    gap();
    connect(add(icons::Id::GoToIn, tr("Go to In (Shift+I)"), true), &QToolButton::clicked, this, &MonitorWidget::goInRequested);
    connect(add(icons::Id::StepBack, tr("Step Back 1 Frame (Left)")), &QToolButton::clicked, this, [this] { emit stepRequested(-1); });
    m_playButton = add(icons::Id::Play, tr("Play-Stop Toggle (Space)"));
    m_playButton->setIconSize(QSize(18, 18));
    connect(m_playButton, &QToolButton::clicked, this, &MonitorWidget::playPauseRequested);
    connect(add(icons::Id::StepForward, tr("Step Forward 1 Frame (Right)")), &QToolButton::clicked, this, [this] { emit stepRequested(1); });
    connect(add(icons::Id::GoToOut, tr("Go to Out (Shift+O)"), true), &QToolButton::clicked, this, &MonitorWidget::goOutRequested);
    gap();
    if (m_role == Role::Source) {
        connect(add(icons::Id::Insert, tr("Insert (,)")), &QToolButton::clicked, this, &MonitorWidget::insertRequested);
        connect(add(icons::Id::Overwrite, tr("Overwrite (.)")), &QToolButton::clicked, this, &MonitorWidget::overwriteRequested);
    } else {
        connect(add(icons::Id::Lift, tr("Lift (;)")), &QToolButton::clicked, this, &MonitorWidget::liftRequested);
        connect(add(icons::Id::Extract, tr("Extract (')")), &QToolButton::clicked, this, &MonitorWidget::extractRequested);
    }
    gap();
    connect(add(icons::Id::ExportFrame, tr("Export Frame (Ctrl+Shift+E)"), true), &QToolButton::clicked, this, &MonitorWidget::exportFrameRequested);
    row->addStretch(1);
    controls->addLayout(row);

    root->addWidget(m_controls);
}

void MonitorWidget::setResolutionDivisor(int divisor)
{
    const int i = m_resolution->findData(divisor);
    if (i >= 0 && i != m_resolution->currentIndex()) {
        QSignalBlocker block(m_resolution);
        m_resolution->setCurrentIndex(i);
    }
}

void MonitorWidget::setSafeMargins(bool on)
{
    m_safeMargins = on;
    if (m_safeAction && m_safeAction->isChecked() != on) m_safeAction->setChecked(on);
    update();
}

void MonitorWidget::setImage(const QImage& image)
{
    if (image.cacheKey() == m_image.cacheKey() && !image.isNull()) return;
    m_image = image;
    m_scaled = QImage();
    update(canvasRect());
}

void MonitorWidget::clearImage()
{
    m_image = QImage();
    m_scaled = QImage();
    update();
}

void MonitorWidget::setTimecode(const QString& text) { m_timecode->setText(text); }
void MonitorWidget::setDurationText(const QString& text) { m_duration->setText(text); }

void MonitorWidget::setInfoText(const QString& text)
{
    m_infoText = text;
    setToolTip(text);
}

void MonitorWidget::setPlaying(bool playing)
{
    m_playing = playing;
    m_playButton->setIcon(icons::icon(playing ? icons::Id::Stop : icons::Id::Play));
    if (!playing) {
        m_scaled = QImage();
        update(canvasRect());
    }
}

void MonitorWidget::setMarks(Tick in, Tick out) { m_scrub->setMarks(in, out); }
void MonitorWidget::setDuration(Tick duration) { m_scrub->setDuration(duration); }
void MonitorWidget::setPosition(Tick position) { m_scrub->setPosition(position); }
void MonitorWidget::setMarkers(const QVector<Tick>& markers) { m_scrub->setMarkers(markers); }

QRect MonitorWidget::canvasRect() const
{
    return QRect(0, 0, width(), std::max(10, height() - m_controls->sizeHint().height()));
}

QRect MonitorWidget::pictureRect() const
{
    const QRect canvas = canvasRect().adjusted(4, 4, -4, -4);
    const QSize source = m_image.isNull() ? m_frameSize : m_image.size();
    QSize target;
    if (m_zoomFactor <= 0.0) {
        target = (m_frameSize.isValid() ? m_frameSize : source).scaled(canvas.size(), Qt::KeepAspectRatio);
    } else {
        target = QSize(static_cast<int>(m_frameSize.width() * m_zoomFactor),
                       static_cast<int>(m_frameSize.height() * m_zoomFactor));
    }
    return QRect(canvas.center().x() - target.width() / 2, canvas.center().y() - target.height() / 2,
                 target.width(), target.height());
}

void MonitorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_scaled = QImage();
    const bool roomy = width() >= 400;
    for (QWidget* w : m_secondary) w->setVisible(roomy);
    m_zoom->setVisible(width() >= 300);
}

void MonitorWidget::mousePressEvent(QMouseEvent* event)
{
    m_pressPos = event->position().toPoint();
    QWidget::mousePressEvent(event);
}

void MonitorWidget::mouseMoveEvent(QMouseEvent* event)
{
    // Dragging the source picture drops the clip on the timeline.
    if (m_role != Role::Source || m_dragMediaId.isEmpty() || !(event->buttons() & Qt::LeftButton)) return;
    if ((event->position().toPoint() - m_pressPos).manhattanLength() < 8) return;
    auto* drag = new QDrag(this);
    auto* data = new QMimeData;
    data->setData(QStringLiteral("application/x-breefer-media"), m_dragMediaId.toUtf8());
    drag->setMimeData(data);
    if (!m_image.isNull()) drag->setPixmap(QPixmap::fromImage(m_image.scaledToWidth(120)));
    drag->exec(Qt::CopyAction);
}

void MonitorWidget::wheelEvent(QWheelEvent* event)
{
    emit stepRequested(event->angleDelta().y() > 0 ? -1 : 1);
}

void MonitorWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QRect canvas = canvasRect();
    painter.setClipRect(event->rect());
    painter.fillRect(canvas, QColor(0x0b, 0x0b, 0x0b));

    const QRect target = pictureRect();
    painter.save();
    painter.setClipRect(canvas.intersected(event->rect()));
    if (!m_image.isNull()) {
        if (m_playing) {
            // Playback: let the painter scale on the fly (cheap, no allocation).
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(target, m_image);
        } else {
            if (m_scaled.size() != target.size())
                m_scaled = m_image.scaled(target.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(target.topLeft(), m_scaled);
        }
    } else {
        painter.fillRect(target, Qt::black);
        if (m_role == Role::Source) {
            painter.setPen(theme::textFaint());
            painter.drawText(canvas, Qt::AlignCenter, tr("(no clips)"));
        }
    }

    if (m_safeMargins) {
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1));
        painter.setBrush(Qt::NoBrush);
        const QRect action = target.adjusted(target.width() * 0.05, target.height() * 0.05,
                                             -target.width() * 0.05, -target.height() * 0.05);
        const QRect title = target.adjusted(target.width() * 0.1, target.height() * 0.1,
                                            -target.width() * 0.1, -target.height() * 0.1);
        painter.drawRect(action);
        painter.drawRect(title);
        const QPoint c = target.center();
        painter.drawLine(c.x() - 8, c.y(), c.x() + 8, c.y());
        painter.drawLine(c.x(), c.y() - 8, c.x(), c.y() + 8);
    }
    if (!m_overlayText.isEmpty()) {
        painter.setPen(QColor(255, 255, 255, 220));
        painter.drawText(canvas.adjusted(10, 6, -10, -6), Qt::AlignLeft | Qt::AlignTop, m_overlayText);
    }
    painter.restore();
}

} // namespace bc
