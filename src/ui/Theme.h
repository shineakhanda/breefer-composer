#pragma once

#include <QColor>
#include <QFont>

class QApplication;

namespace bc::theme {

// Premiere Pro (Spectrum dark) palette: near-black gutters between rounded
// #1d1d1d panels, a blue outline on the focused panel, blue accents.
inline QColor windowBg()      { return QColor(0x10, 0x10, 0x10); }   // gutters
inline QColor panelBg()       { return QColor(0x1d, 0x1d, 0x1d); }
inline QColor panelAlt()      { return QColor(0x25, 0x25, 0x25); }
inline QColor headerBg()      { return QColor(0x1d, 0x1d, 0x1d); }
inline QColor headerActive()  { return QColor(0x2a, 0x2a, 0x2a); }
inline QColor border()        { return QColor(0x2c, 0x2c, 0x2c); }
inline QColor borderSoft()    { return QColor(0x15, 0x15, 0x15); }
inline QColor text()          { return QColor(0xe3, 0xe3, 0xe3); }
inline QColor textDim()       { return QColor(0x9b, 0x9b, 0x9b); }
inline QColor textFaint()     { return QColor(0x66, 0x66, 0x66); }
inline QColor accent()        { return QColor(0x37, 0x8e, 0xf0); }
inline QColor accentDim()     { return QColor(0x26, 0x6b, 0xc0); }
inline QColor selection()     { return QColor(0x2a, 0x5f, 0xa6); }
inline QColor playhead()      { return QColor(0x3b, 0x9b, 0xff); }
inline QColor playheadDim()   { return QColor(0x26, 0x6b, 0xc0); }
inline QColor hotText()       { return QColor(0x4b, 0xa3, 0xff); }   // scrubbable values
inline QColor danger()        { return QColor(0xd9, 0x5b, 0x55); }

// Timeline surfaces.
inline QColor rulerBg()       { return QColor(0x1d, 0x1d, 0x1d); }
inline QColor trackVideoBg(bool) { return QColor(0x17, 0x17, 0x17); }
inline QColor trackAudioBg(bool) { return QColor(0x17, 0x17, 0x17); }
inline QColor trackLine()     { return QColor(0x24, 0x24, 0x24); }
inline QColor gridLine()      { return QColor(0x20, 0x20, 0x20); }

inline QColor videoClipBody() { return QColor(0x8f, 0x8b, 0xe6); }
inline QColor videoClipBodySelected() { return QColor(0xb4, 0xb1, 0xff); }
inline QColor audioClipBody() { return QColor(0x2f, 0xa5, 0x7a); }
inline QColor audioClipBodySelected() { return QColor(0x4f, 0xc9, 0x9a); }

// Premiere's label colours: Violet, Iris, Caribbean, Lavender, Cerulean,
// Forest, Rose, Mango.
inline QColor labelColor(int index)
{
    static const QColor colors[] = {
        QColor(0xa5, 0x8f, 0xe8),   // Violet
        QColor(0x8f, 0x8b, 0xe6),   // Iris
        QColor(0x1f, 0xb0, 0x9e),   // Caribbean
        QColor(0xe0, 0x8a, 0xe3),   // Lavender
        QColor(0x2d, 0x9c, 0xe0),   // Cerulean
        QColor(0x4c, 0xae, 0x55),   // Forest
        QColor(0xf0, 0x71, 0x9e),   // Rose
        QColor(0xf2, 0x9d, 0x38),   // Mango
    };
    return colors[qAbs(index) % 8];
}
inline const char* labelName(int index)
{
    static const char* names[] = {"Violet", "Iris", "Caribbean", "Lavender", "Cerulean", "Forest", "Rose", "Mango"};
    return names[qAbs(index) % 8];
}

inline QFont uiFont(int pointSize = 8, bool bold = false)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSizeF(pointSize);
    f.setBold(bold);
    return f;
}

inline QFont monoFont(int pointSize = 8)
{
    QFont f(QStringLiteral("Consolas"));
    f.setPointSizeF(pointSize);
    return f;
}

void apply(QApplication& app);

} // namespace bc::theme
