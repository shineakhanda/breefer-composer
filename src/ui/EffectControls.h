#pragma once

#include "core/Project.h"

#include <QSet>
#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace bc {

class Document;
class ScrubValue;
class KeyframeLane;

// Premiere-style Effect Controls: twirl-down effects with fx bypass, reset,
// stopwatch animation toggles, scrubbable blue values, keyframe navigation
// and a keyframe lane showing diamonds against the clip's time.
class EffectControls : public QWidget {
    Q_OBJECT

public:
    explicit EffectControls(Document* document, QWidget* parent = nullptr);

signals:
    void editTitleRequested(const QString& clipId);

public slots:
    void refresh();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Row {
        QString effectId;
        QString paramId;
        ScrubValue* value = nullptr;
        KeyframeLane* lane = nullptr;
        QWidget* nav = nullptr;
    };

    void rebuild();
    void updateValues();
    void beginLive();
    void endLive(const QString& text);
    Clip* liveClip();
    Param* liveParam(const QString& effectId, const QString& paramId);
    void editParam(const QString& effectId, const QString& paramId, const std::function<void(Param&, Tick local)>& fn,
                   const QString& text);
    void editClip(const std::function<bool(Clip&)>& fn, const QString& text);

    Document* m_document;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_layout = nullptr;
    QLabel* m_header = nullptr;

    QString m_clipId;
    QVector<Row> m_rows;
    QSet<QString> m_collapsed;      // effect ids twirled closed
    Sequence m_before;
    bool m_live = false;
    bool m_showLanes = true;
};

// Keyframe diamonds for one parameter across the clip's duration.
class KeyframeLane : public QWidget {
    Q_OBJECT

public:
    explicit KeyframeLane(QWidget* parent = nullptr);
    void setData(Tick clipDuration, Tick playheadLocal, const QVector<Keyframe>& keys);

signals:
    void seekRequested(bc::Tick local);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    Tick m_duration = 0;
    Tick m_playhead = 0;
    QVector<Keyframe> m_keys;
};

} // namespace bc
