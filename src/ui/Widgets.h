#pragma once

#include "ui/Icons.h"

#include <QFrame>
#include <QTabWidget>
#include <QToolButton>
#include <functional>

class QLabel;
class QLineEdit;
class QMenu;

namespace bc {

// Rounded panel on the black gutter; shows a blue outline when it holds focus.
class PanelFrame : public QFrame {
    Q_OBJECT

public:
    explicit PanelFrame(QWidget* content, QWidget* parent = nullptr);
    QWidget* content() const { return m_content; }
    void setActive(bool active);

private:
    QWidget* m_content;
};

// Premiere panel group: bold tab titles on top and a hamburger panel menu.
class PanelTabs : public QTabWidget {
    Q_OBJECT

public:
    explicit PanelTabs(QWidget* parent = nullptr);

    // Builds the hamburger menu for the current tab on demand.
    void setMenuProvider(std::function<void(int index, QMenu* menu)> provider) { m_provider = std::move(provider); }
    int addPanel(QWidget* widget, const QString& title);
    void raisePanel(QWidget* widget);

signals:
    void maximizeRequested();

private:
    std::function<void(int, QMenu*)> m_provider;
};

// Flat icon button (Premiere style). Checkable buttons turn blue when on.
QToolButton* iconButton(icons::Id id, const QString& tip, QWidget* parent, bool checkable = false, int size = 18);

// Premiere "hot text": blue number, drag horizontally to scrub (Shift = x10,
// Ctrl = x0.1), click to type a value.
class ScrubValue : public QWidget {
    Q_OBJECT

public:
    explicit ScrubValue(QWidget* parent = nullptr);

    void setRange(double minimum, double maximum) { m_min = minimum; m_max = maximum; }
    void setDecimals(int decimals) { m_decimals = decimals; update(); }
    void setSuffix(const QString& suffix) { m_suffix = suffix; update(); }
    void setStepPerPixel(double step) { m_step = step; }
    void setValue(double value);
    double value() const { return m_value; }
    QSize sizeHint() const override;

signals:
    void editStarted();
    void valueEdited(double value);     // live while scrubbing
    void editFinished(double value);    // mouse release or Enter

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void beginTyping();
    QString text() const;

    double m_value = 0.0;
    double m_min = -1e9;
    double m_max = 1e9;
    double m_step = 0.1;
    int m_decimals = 1;
    QString m_suffix;
    QPoint m_pressPos;
    double m_pressValue = 0.0;
    bool m_dragging = false;
    bool m_pressed = false;
    QLineEdit* m_editor = nullptr;
};

} // namespace bc
