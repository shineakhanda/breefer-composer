#pragma once

#include "core/Types.h"

#include <QImage>
#include <QWidget>

class QComboBox;
class QLabel;
class QToolButton;
class QAction;

namespace bc {

// Mini timeline under the picture: duration, playhead, in/out range, markers.
class ScrubBar : public QWidget {
    Q_OBJECT

public:
    explicit ScrubBar(QWidget* parent = nullptr);
    void setDuration(Tick duration) { m_duration = duration; update(); }
    void setPosition(Tick position);
    void setMarks(Tick in, Tick out) { m_in = in; m_out = out; update(); }
    void setMarkers(const QVector<Tick>& markers) { m_markers = markers; update(); }

signals:
    void scrubbed(bc::Tick time);
    void scrubStarted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    Tick tickAt(int x) const;
    int xOf(Tick t) const;
    Tick m_duration = 0;
    Tick m_position = 0;
    Tick m_in = -1;
    Tick m_out = -1;
    QVector<Tick> m_markers;
};

class MonitorWidget : public QWidget {
    Q_OBJECT

public:
    enum class Role { Source, Program };

    explicit MonitorWidget(Role role, QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    void setTimecode(const QString& text);
    void setDurationText(const QString& text);
    void setInfoText(const QString& text);
    void setPlaying(bool playing);
    void setMarks(Tick in, Tick out);
    void setDuration(Tick duration);
    void setPosition(Tick position);
    void setMarkers(const QVector<Tick>& markers);
    void setFrameSize(QSize size) { m_frameSize = size; m_scaled = QImage(); update(); }
    void setOverlayText(const QString& text) { m_overlayText = text; update(); }
    void setSafeMargins(bool on);
    bool safeMargins() const { return m_safeMargins; }
    void setDragMediaId(const QString& id) { m_dragMediaId = id; }
    void setResolutionDivisor(int divisor);
    QImage currentImage() const { return m_image; }

signals:
    void playPauseRequested();
    void goStartRequested();
    void goEndRequested();
    void stepRequested(int frames);
    void setInRequested();
    void setOutRequested();
    void goInRequested();
    void goOutRequested();
    void insertRequested();
    void overwriteRequested();
    void liftRequested();
    void extractRequested();
    void exportFrameRequested();
    void addMarkerRequested();
    void scrubbed(bc::Tick time);
    void scrubStarted();
    void loopToggled(bool on);
    void resolutionChanged(int divisor);   // 1, 2, 4, 8, 16

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRect canvasRect() const;
    QRect pictureRect() const;
    void buildUi();
    void layoutTransport();

    Role m_role;
    QImage m_image;
    QImage m_scaled;
    QSize m_frameSize{1920, 1080};

    QWidget* m_controls = nullptr;
    QLabel* m_timecode = nullptr;
    QLabel* m_duration = nullptr;
    QComboBox* m_zoom = nullptr;
    QComboBox* m_resolution = nullptr;
    QToolButton* m_wrench = nullptr;
    QToolButton* m_playButton = nullptr;
    ScrubBar* m_scrub = nullptr;
    QList<QWidget*> m_secondary;   // hidden first when the monitor is narrow
    QAction* m_safeAction = nullptr;

    QString m_overlayText;
    QString m_infoText;
    QString m_dragMediaId;
    QPoint m_pressPos;
    double m_zoomFactor = 0.0;    // 0 = Fit
    bool m_safeMargins = false;
    bool m_playing = false;
};

} // namespace bc
