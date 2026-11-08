#pragma once

#include <QWidget>

namespace bc {

class AudioMeters : public QWidget {
    Q_OBJECT

public:
    explicit AudioMeters(QWidget* parent = nullptr);

    void setLevels(float left, float right);
    void reset();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_left = 0.0f;
    float m_right = 0.0f;
    float m_holdLeft = 0.0f;
    float m_holdRight = 0.0f;
};

} // namespace bc
