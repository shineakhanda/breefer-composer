#pragma once

#include "core/Project.h"

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace bc {

class Document;

// Lumetri-style colour panel: basic correction + creative look, writing into
// the clip's "lumetri" effect parameters.
class LumetriPanel : public QWidget {
    Q_OBJECT

public:
    explicit LumetriPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void addParamRow(QVBoxLayout* layout, const char* paramId, const QString& label,
                     double minimum, double maximum, const QString& suffix);
    void beginLiveEdit();
    void applyLive(const char* paramId, double value);
    void commitLiveEdit(const QString& text);

    Document* m_document;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_layout = nullptr;

    QString m_clipId;
    int m_trackIndex = -1;
    bool m_live = false;
    QVector<Clip> m_before;
};

// Waveform, RGB parade and vectorscope computed from the program frame.
class ScopesPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScopesPanel(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setMode(int mode) { m_mode = mode; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage drawWaveform(const QImage& source) const;
    QImage drawParade(const QImage& source) const;
    QImage drawVectorscope(const QImage& source) const;

    QImage m_image;
    int m_mode = 0;   // 0 wave, 1 parade, 2 vector
    mutable QElapsedTimer m_throttle;
};

} // namespace bc
