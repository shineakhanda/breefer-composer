#include "ui/AudioMeters.h"

#include "ui/Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace bc {
namespace {

float toNormalized(float linear)
{
    if (linear <= 1e-5f) return 0.0f;
    const float db = 20.0f * std::log10(linear);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

} // namespace

AudioMeters::AudioMeters(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(96);
    setMinimumHeight(120);
}

void AudioMeters::setLevels(float left, float right)
{
    m_left = std::clamp(left, 0.0f, 1.0f);
    m_right = std::clamp(right, 0.0f, 1.0f);
    m_holdLeft = std::max(m_holdLeft - 0.01f, m_left);
    m_holdRight = std::max(m_holdRight - 0.01f, m_right);
    update();
}

void AudioMeters::reset()
{
    m_left = m_right = m_holdLeft = m_holdRight = 0.0f;
    update();
}

void AudioMeters::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x17, 0x17, 0x19));

    const int pad = 10;
    const int labelW = 26;
    const int barW = 14;
    const int gap = 8;
    const int top = pad;
    const int bottom = height() - pad - 14;
    const int usable = bottom - top;
    if (usable < 20) return;

    const int contentWidth = labelW + barW * 2 + gap;
    const int x1 = std::max(pad + labelW, (width() - contentWidth) / 2 + labelW);
    const int x2 = x1 + barW + gap;

    p.setFont(theme::monoFont(7));
    p.setPen(theme::textDim());
    for (int db = 0; db >= -60; db -= 12) {
        const float t = (60.0f + static_cast<float>(db)) / 60.0f;
        const int y = bottom - static_cast<int>(t * usable);
        p.drawText(QRect(0, y - 6, pad + labelW - 4, 12), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(db));
        p.setPen(QColor(0x2c, 0x2c, 0x32));
        p.drawLine(x1, y, x2 + barW, y);
        p.setPen(theme::textDim());
    }

    auto drawBar = [&](int x, float level, float hold) {
        p.fillRect(QRect(x, top, barW, usable), QColor(0x21, 0x21, 0x25));

        const float norm = toNormalized(level);
        const int filled = static_cast<int>(norm * usable);

        QLinearGradient grad(0, static_cast<qreal>(bottom), 0, static_cast<qreal>(top));
        grad.setColorAt(0.0, QColor(0x3d, 0xc9, 0x6b));
        grad.setColorAt(0.7, QColor(0xd4, 0xd4, 0x4a));
        grad.setColorAt(1.0, QColor(0xe0, 0x4a, 0x4a));

        p.fillRect(QRect(x, bottom - filled, barW, filled), grad);

        const int holdY = bottom - static_cast<int>(toNormalized(hold) * usable);
        p.setPen(QPen(QColor(0xf0, 0xf0, 0xf4), 1));
        p.drawLine(x, holdY, x + barW - 1, holdY);
    };

    drawBar(x1, m_left, m_holdLeft);
    drawBar(x2, m_right, m_holdRight);

    p.setPen(theme::textDim());
    p.setFont(theme::monoFont(7));
    p.drawText(QRect(x1 - 4, bottom + 2, barW + 8, 12), Qt::AlignCenter, QStringLiteral("L"));
    p.drawText(QRect(x2 - 4, bottom + 2, barW + 8, 12), Qt::AlignCenter, QStringLiteral("R"));
}

} // namespace bc
