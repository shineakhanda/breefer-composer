#include "ui/Widgets.h"

#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QTabBar>
#include <QVBoxLayout>
#include <cmath>

namespace bc {

// ---------------------------------------------------------------- PanelFrame

PanelFrame::PanelFrame(QWidget* content, QWidget* parent)
    : QFrame(parent)
    , m_content(content)
{
    setObjectName(QStringLiteral("PanelFrame"));
    setProperty("active", false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(content);
}

void PanelFrame::setActive(bool active)
{
    if (property("active").toBool() == active) return;
    setProperty("active", active);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

// ----------------------------------------------------------------- PanelTabs

PanelTabs::PanelTabs(QWidget* parent)
    : QTabWidget(parent)
{
    setObjectName(QStringLiteral("PanelTabs"));
    tabBar()->setObjectName(QStringLiteral("PanelTabBar"));
    tabBar()->setDrawBase(false);
    tabBar()->setExpanding(false);
    tabBar()->setElideMode(Qt::ElideRight);
    tabBar()->setUsesScrollButtons(true);
    setDocumentMode(true);

    auto* menuButton = iconButton(icons::Id::Hamburger, tr("Panel menu"), this, false, 16);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    menuButton->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        if (m_provider) m_provider(currentIndex(), menu);
        if (!menu->isEmpty()) menu->addSeparator();
        menu->addAction(tr("Maximize Frame  (`)"), this, &PanelTabs::maximizeRequested);
    });
    setCornerWidget(menuButton, Qt::TopRightCorner);

    // Double-click a tab to maximize the frame, as in Premiere.
    connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int) { emit maximizeRequested(); });
}

int PanelTabs::addPanel(QWidget* widget, const QString& title)
{
    return addTab(widget, title);
}

void PanelTabs::raisePanel(QWidget* widget)
{
    const int i = indexOf(widget);
    if (i >= 0) setCurrentIndex(i);
}

// ----------------------------------------------------------------- DockPanel

DockPanel::DockPanel(const QString& id, const QString& title, QWidget* content, QWidget* parent)
    : QDockWidget(title, parent)
    , m_content(content)
{
    setObjectName(id);   // required by QMainWindow::saveState
    m_frame = new PanelFrame(content, this);
    setWidget(m_frame);
    setFeatures(DockWidgetClosable | DockWidgetMovable | DockWidgetFloatable);
    setAllowedAreas(Qt::AllDockWidgetAreas);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("DockTitle"));
    auto* row = new QHBoxLayout(m_titleBar);
    row->setContentsMargins(0, 0, 2, 0);
    row->setSpacing(0);
    m_titleLabel = new QLabel(title, m_titleBar);
    m_titleLabel->setObjectName(QStringLiteral("PanelTitle"));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);   // drag the whole bar
    row->addWidget(m_titleLabel, 1);

    m_menuButton = iconButton(icons::Id::Hamburger, tr("Panel menu"), m_titleBar, false, 16);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_menuButton);
    m_menuButton->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        if (m_provider) m_provider(menu);
        if (!menu->isEmpty()) menu->addSeparator();
        menu->addAction(tr("Maximize Frame  (`)"), this, &DockPanel::maximizeRequested);
        menu->addAction(isFloating() ? tr("Dock Panel") : tr("Undock Panel (float / second monitor)"), this,
                        [this] { setFloating(!isFloating()); });
        menu->addAction(tr("Close Panel"), this, &QWidget::close);
    });
    row->addWidget(m_menuButton);
    setTitleBarWidget(m_titleBar);
    connect(this, &QDockWidget::windowTitleChanged, m_titleLabel, &QLabel::setText);
}

void DockPanel::setCompact(bool compact)
{
    m_compact = compact;
    m_titleLabel->setVisible(!compact && !m_tabbed);
    m_menuButton->setVisible(!compact);
    m_titleBar->setFixedHeight(compact ? 7 : QWIDGETSIZE_MAX);
    m_titleBar->setMinimumHeight(compact ? 7 : 0);
    m_titleBar->setToolTip(compact ? windowTitle() + tr(" - drag this strip to move the panel") : QString());
    m_titleBar->setProperty("compact", compact);
    m_titleBar->style()->unpolish(m_titleBar);
    m_titleBar->style()->polish(m_titleBar);
}

void DockPanel::setTabbed(bool tabbed)
{
    if (m_tabbed == tabbed) return;
    m_tabbed = tabbed;
    if (!m_compact) m_titleLabel->setVisible(!tabbed);
}

void DockPanel::setTitle(const QString& title)
{
    setWindowTitle(title);
}

// ---------------------------------------------------------------- iconButton

QToolButton* iconButton(icons::Id id, const QString& tip, QWidget* parent, bool checkable, int size)
{
    auto* b = new QToolButton(parent);
    b->setIcon(icons::icon(id));
    b->setIconSize(QSize(size, size));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(size + 8, size + 8);
    return b;
}

// ---------------------------------------------------------------- ScrubValue

ScrubValue::ScrubValue(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumWidth(44);
    setFixedHeight(18);
}

QSize ScrubValue::sizeHint() const
{
    return QSize(std::max(44, fontMetrics().horizontalAdvance(text()) + 8), 18);
}

QString ScrubValue::text() const
{
    return QString::number(m_value, 'f', m_decimals) + m_suffix;
}

void ScrubValue::setValue(double value)
{
    value = std::clamp(value, m_min, m_max);
    if (value == m_value) return;
    m_value = value;
    updateGeometry();
    update();
}

void ScrubValue::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setFont(font());
    const QColor c = isEnabled() ? theme::hotText() : theme::textFaint();
    p.setPen(c);
    const QString t = text();
    const QRect r = rect().adjusted(2, 0, -2, -2);
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, t);
    // Dotted underline marks scrubbable text.
    QPen pen(c, 1, Qt::DotLine);
    p.setPen(pen);
    const int w = std::min(r.width(), fontMetrics().horizontalAdvance(t));
    p.drawLine(r.left(), r.bottom(), r.left() + w, r.bottom());
}

void ScrubValue::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_pressed = true;
    m_dragging = false;
    m_pressPos = event->globalPosition().toPoint();
    m_pressValue = m_value;
}

void ScrubValue::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) return;
    const int dx = event->globalPosition().toPoint().x() - m_pressPos.x();
    if (!m_dragging && std::abs(dx) < 3) return;
    if (!m_dragging) {
        m_dragging = true;
        emit editStarted();
    }
    double factor = 1.0;
    if (event->modifiers() & Qt::ShiftModifier) factor = 10.0;
    if (event->modifiers() & Qt::ControlModifier) factor = 0.1;
    const double v = std::clamp(m_pressValue + dx * m_step * factor, m_min, m_max);
    const double rounded = std::round(v * std::pow(10.0, m_decimals)) / std::pow(10.0, m_decimals);
    if (rounded != m_value) {
        m_value = rounded;
        update();
        emit valueEdited(m_value);
    }
}

void ScrubValue::mouseReleaseEvent(QMouseEvent*)
{
    if (!m_pressed) return;
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        emit editFinished(m_value);
        return;
    }
    beginTyping();
}

void ScrubValue::beginTyping()
{
    if (m_editor) return;
    m_editor = new QLineEdit(QString::number(m_value, 'f', m_decimals), this);
    m_editor->setGeometry(rect());
    m_editor->setStyleSheet(QStringLiteral("QLineEdit { padding: 0 2px; border: 1px solid #378ef0; }"));
    m_editor->selectAll();
    m_editor->show();
    m_editor->setFocus();
    auto finish = [this](bool accept) {
        if (!m_editor) return;
        QLineEdit* editor = m_editor;
        m_editor = nullptr;
        if (accept) {
            bool ok = false;
            const double v = editor->text().toDouble(&ok);
            if (ok) {
                emit editStarted();
                m_value = std::clamp(v, m_min, m_max);
                update();
                emit editFinished(m_value);
            }
        }
        editor->deleteLater();
    };
    connect(m_editor, &QLineEdit::returnPressed, this, [finish] { finish(true); });
    connect(m_editor, &QLineEdit::editingFinished, this, [finish] { finish(true); });
}

} // namespace bc
