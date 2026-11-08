#include "ui/Icons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace bc::icons {
namespace {

// All glyphs are drawn on a 20x20 grid with 1.5 px strokes.
void draw(QPainter& p, Id id, const QColor& c)
{
    const QPen line(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen thin(c, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    auto fill = [&](const QPainterPath& path) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(path);
        p.restore();
    };
    auto poly = [](std::initializer_list<QPointF> pts) {
        QPainterPath path;
        bool first = true;
        for (const QPointF& pt : pts) {
            if (first) path.moveTo(pt);
            else path.lineTo(pt);
            first = false;
        }
        path.closeSubpath();
        return path;
    };
    auto arrowHead = [&](QPointF tip, double dx, double size = 3.0) {
        // Filled triangle pointing along +dx/-dx horizontally.
        fill(poly({tip, {tip.x() - dx * size, tip.y() - size * 0.9}, {tip.x() - dx * size, tip.y() + size * 0.9}}));
    };

    switch (id) {
    // ------------------------------------------------------------ tools
    case Id::Selection:
        fill(poly({{6, 2.5}, {6, 16.5}, {9.4, 13.2}, {11.8, 18.2}, {13.8, 17.3}, {11.4, 12.3}, {16, 12.3}}));
        break;
    case Id::TrackSelectForward:
        fill(poly({{3, 4}, {3, 13}, {5.2, 10.9}, {6.8, 14.2}, {8.1, 13.6}, {6.5, 10.3}, {9.4, 10.3}}));
        fill(poly({{9.5, 6}, {9.5, 15}, {11.7, 12.9}, {13.3, 16.2}, {14.6, 15.6}, {13, 12.3}, {15.9, 12.3}}));
        p.drawLine(QPointF(17.5, 3.5), QPointF(17.5, 16.5));
        break;
    case Id::RippleEdit:
        p.drawLine(QPointF(9, 3.5), QPointF(9, 16.5));
        p.drawLine(QPointF(9, 3.5), QPointF(12, 3.5));
        p.drawLine(QPointF(9, 16.5), QPointF(12, 16.5));
        p.drawLine(QPointF(10.5, 10), QPointF(17, 10));
        arrowHead({18, 10}, 1, 2.6);
        p.drawLine(QPointF(3, 10), QPointF(6.5, 10));
        break;
    case Id::RollingEdit:
        p.drawLine(QPointF(8.5, 3.5), QPointF(8.5, 16.5));
        p.drawLine(QPointF(11.5, 3.5), QPointF(11.5, 16.5));
        p.drawLine(QPointF(8.5, 3.5), QPointF(6.5, 3.5));
        p.drawLine(QPointF(8.5, 16.5), QPointF(6.5, 16.5));
        p.drawLine(QPointF(11.5, 3.5), QPointF(13.5, 3.5));
        p.drawLine(QPointF(11.5, 16.5), QPointF(13.5, 16.5));
        arrowHead({2.5, 10}, -1, 2.6);
        arrowHead({17.5, 10}, 1, 2.6);
        p.drawLine(QPointF(4, 10), QPointF(7, 10));
        p.drawLine(QPointF(13, 10), QPointF(16, 10));
        break;
    case Id::RateStretch:
        p.drawLine(QPointF(3, 4), QPointF(3, 16));
        p.drawLine(QPointF(17, 4), QPointF(17, 16));
        p.drawLine(QPointF(5.5, 10), QPointF(14.5, 10));
        arrowHead({5, 10}, -1, 2.6);
        arrowHead({15, 10}, 1, 2.6);
        p.setPen(thin);
        p.drawLine(QPointF(6, 6), QPointF(14, 6));
        p.drawLine(QPointF(6, 14), QPointF(14, 14));
        break;
    case Id::Razor: {
        // Straight razor: diagonal blade with a handle, as in Premiere.
        p.save();
        p.translate(10, 10);
        p.rotate(-45);
        QPainterPath blade;
        blade.moveTo(-8, -3);
        blade.lineTo(3, -3);
        blade.lineTo(3, 2.2);
        blade.lineTo(-6.5, 2.2);
        blade.quadTo(-8, 2.2, -8, 0.5);
        blade.closeSubpath();
        p.drawPath(blade);
        fill([] { QPainterPath h; h.addRoundedRect(QRectF(3.5, -1.6, 6, 3.2), 1.2, 1.2); return h; }());
        p.restore();
        break;
    }
    case Id::Slip:
        p.drawLine(QPointF(4, 4), QPointF(4, 16));
        p.drawLine(QPointF(16, 4), QPointF(16, 16));
        p.drawLine(QPointF(6.5, 10), QPointF(13.5, 10));
        arrowHead({6, 10}, -1, 2.4);
        arrowHead({14, 10}, 1, 2.4);
        p.setPen(thin);
        p.drawRect(QRectF(7, 5, 6, 2.5));
        p.drawRect(QRectF(7, 12.5, 6, 2.5));
        break;
    case Id::Slide:
        p.drawRect(QRectF(7, 5.5, 6, 9));
        p.drawLine(QPointF(2, 10), QPointF(5, 10));
        p.drawLine(QPointF(15, 10), QPointF(18, 10));
        arrowHead({1.6, 10}, -1, 2.2);
        arrowHead({18.4, 10}, 1, 2.2);
        break;
    case Id::Hand: {
        QPainterPath hand;
        hand.moveTo(6, 11);
        hand.lineTo(6, 5.5);
        hand.quadTo(6, 4.2, 7.2, 4.2);
        hand.quadTo(8.4, 4.2, 8.4, 5.5);
        hand.lineTo(8.4, 9);
        hand.lineTo(8.4, 3.7);
        hand.quadTo(8.4, 2.4, 9.6, 2.4);
        hand.quadTo(10.8, 2.4, 10.8, 3.7);
        hand.lineTo(10.8, 9);
        hand.lineTo(10.8, 4.4);
        hand.quadTo(10.8, 3.2, 12, 3.2);
        hand.quadTo(13.2, 3.2, 13.2, 4.4);
        hand.lineTo(13.2, 9.5);
        hand.lineTo(13.2, 6.4);
        hand.quadTo(13.2, 5.3, 14.3, 5.3);
        hand.quadTo(15.4, 5.3, 15.4, 6.4);
        hand.lineTo(15.4, 12);
        hand.quadTo(15.4, 17.5, 10.5, 17.5);
        hand.quadTo(7.5, 17.5, 6, 14.5);
        hand.lineTo(3.6, 10.8);
        hand.quadTo(3.2, 9.6, 4.3, 9.4);
        hand.quadTo(5.2, 9.3, 6, 11);
        p.drawPath(hand);
        break;
    }
    case Id::Zoom:
    case Id::Search:
        p.drawEllipse(QPointF(8.5, 8.5), 5, 5);
        p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(12.3, 12.3), QPointF(16.8, 16.8));
        if (id == Id::Zoom) {
            p.setPen(thin);
            p.drawLine(QPointF(6.3, 8.5), QPointF(10.7, 8.5));
            p.drawLine(QPointF(8.5, 6.3), QPointF(8.5, 10.7));
        }
        break;
    case Id::Type:
        p.drawLine(QPointF(4, 4.5), QPointF(16, 4.5));
        p.drawLine(QPointF(4, 4.5), QPointF(4, 6.5));
        p.drawLine(QPointF(16, 4.5), QPointF(16, 6.5));
        p.drawLine(QPointF(10, 4.5), QPointF(10, 16));
        p.drawLine(QPointF(7.5, 16), QPointF(12.5, 16));
        break;

    // ------------------------------------------------------- transport
    case Id::AddMarker:
        fill(poly({{5.5, 3}, {14.5, 3}, {14.5, 11}, {10, 16}, {5.5, 11}}));
        break;
    case Id::MarkIn:
        p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawLine(QPointF(8, 3.5), QPointF(8, 16.5));
        p.drawLine(QPointF(8, 3.5), QPointF(12.5, 3.5));
        p.drawLine(QPointF(8, 16.5), QPointF(12.5, 16.5));
        break;
    case Id::MarkOut:
        p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawLine(QPointF(12, 3.5), QPointF(12, 16.5));
        p.drawLine(QPointF(12, 3.5), QPointF(7.5, 3.5));
        p.drawLine(QPointF(12, 16.5), QPointF(7.5, 16.5));
        break;
    case Id::GoToIn:
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawLine(QPointF(14, 4.5), QPointF(14, 15.5));
        p.drawLine(QPointF(14, 4.5), QPointF(17, 4.5));
        p.drawLine(QPointF(14, 15.5), QPointF(17, 15.5));
        fill(poly({{3, 10}, {11.5, 4.8}, {11.5, 15.2}}));
        break;
    case Id::GoToOut:
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawLine(QPointF(6, 4.5), QPointF(6, 15.5));
        p.drawLine(QPointF(6, 4.5), QPointF(3, 4.5));
        p.drawLine(QPointF(6, 15.5), QPointF(3, 15.5));
        fill(poly({{17, 10}, {8.5, 4.8}, {8.5, 15.2}}));
        break;
    case Id::StepBack:
        fill(poly({{4.5, 10}, {13, 4.8}, {13, 15.2}}));
        p.setPen(QPen(c, 1.8));
        p.drawLine(QPointF(15.5, 5), QPointF(15.5, 15));
        break;
    case Id::StepForward:
        fill(poly({{15.5, 10}, {7, 4.8}, {7, 15.2}}));
        p.setPen(QPen(c, 1.8));
        p.drawLine(QPointF(4.5, 5), QPointF(4.5, 15));
        break;
    case Id::Play:
        fill(poly({{6, 3.5}, {16.5, 10}, {6, 16.5}}));
        break;
    case Id::Stop:
        fill([] { QPainterPath r; r.addRoundedRect(QRectF(5, 5, 10, 10), 1, 1); return r; }());
        break;
    case Id::ShuttleBack:
        fill(poly({{3, 10}, {10, 5}, {10, 15}}));
        fill(poly({{10, 10}, {17, 5}, {17, 15}}));
        break;
    case Id::Insert: {
        // Clip with downward arrow pushing into a gap.
        p.drawLine(QPointF(2.5, 15.5), QPointF(7, 15.5));
        p.drawLine(QPointF(13, 15.5), QPointF(17.5, 15.5));
        p.drawLine(QPointF(10, 3), QPointF(10, 11));
        fill(poly({{10, 14.5}, {6.5, 10}, {13.5, 10}}));
        break;
    }
    case Id::Overwrite:
        p.drawLine(QPointF(2.5, 15.5), QPointF(17.5, 15.5));
        p.drawLine(QPointF(10, 3), QPointF(10, 9.5));
        fill(poly({{10, 13}, {6.5, 8.5}, {13.5, 8.5}}));
        break;
    case Id::Lift:
        p.drawLine(QPointF(2.5, 15.5), QPointF(7, 15.5));
        p.drawLine(QPointF(13, 15.5), QPointF(17.5, 15.5));
        p.drawLine(QPointF(10, 16), QPointF(10, 8));
        fill(poly({{10, 3.5}, {6.5, 8}, {13.5, 8}}));
        break;
    case Id::Extract:
        p.drawLine(QPointF(2.5, 15.5), QPointF(17.5, 15.5));
        p.drawLine(QPointF(10, 13), QPointF(10, 8));
        fill(poly({{10, 3.5}, {6.5, 8}, {13.5, 8}}));
        p.setPen(thin);
        p.drawLine(QPointF(4, 12.5), QPointF(7, 12.5));
        p.drawLine(QPointF(13, 12.5), QPointF(16, 12.5));
        break;
    case Id::ExportFrame: {
        QPainterPath cam;
        cam.addRoundedRect(QRectF(2.5, 6, 15, 10.5), 1.8, 1.8);
        p.drawPath(cam);
        p.drawLine(QPointF(7, 6), QPointF(8.3, 3.8));
        p.drawLine(QPointF(8.3, 3.8), QPointF(11.7, 3.8));
        p.drawLine(QPointF(11.7, 3.8), QPointF(13, 6));
        p.drawEllipse(QPointF(10, 11.2), 3, 3);
        break;
    }
    case Id::Loop: {
        QPainterPath path;
        path.moveTo(5, 12.5);
        path.lineTo(5, 9);
        path.quadTo(5, 6, 8, 6);
        path.lineTo(15, 6);
        p.drawPath(path);
        arrowHead({17.2, 6}, 1, 2.4);
        QPainterPath back;
        back.moveTo(15, 7.5);
        back.lineTo(15, 11);
        back.quadTo(15, 14, 12, 14);
        back.lineTo(5, 14);
        p.drawPath(back);
        arrowHead({2.8, 14}, -1, 2.4);
        break;
    }
    case Id::SafeMargins:
        p.drawRect(QRectF(2.5, 4, 15, 12));
        p.setPen(QPen(c, 1.0, Qt::DashLine));
        p.drawRect(QRectF(5, 6.5, 10, 7));
        break;
    case Id::Wrench: {
        QPainterPath w;
        w.moveTo(12.5, 2.8);
        w.arcTo(QRectF(10, 2.5, 7.5, 7.5), 120, 290);
        p.save();
        p.translate(0, 0);
        p.restore();
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(10.5, 3, 6.5, 6.5), 60 * 16, 270 * 16);
        p.setPen(QPen(c, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11.3, 8.7), QPointF(4, 16));
        break;
    }
    case Id::Plus:
        p.drawLine(QPointF(10, 4), QPointF(10, 16));
        p.drawLine(QPointF(4, 10), QPointF(16, 10));
        break;

    // -------------------------------------------------------- timeline
    case Id::Snap: {
        // Horseshoe magnet.
        p.setPen(QPen(c, 2.6, Qt::SolidLine, Qt::FlatCap));
        QPainterPath m;
        m.moveTo(5.5, 3);
        m.lineTo(5.5, 10);
        m.arcTo(QRectF(5.5, 5.5, 9, 9), 180, 180);
        m.lineTo(14.5, 3);
        p.drawPath(m);
        p.setPen(QPen(c, 2.6, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(3.8, 5.3), QPointF(7.2, 5.3));
        p.drawLine(QPointF(12.8, 5.3), QPointF(16.2, 5.3));
        break;
    }
    case Id::LinkedSelection:
        p.drawRoundedRect(QRectF(2.5, 7, 9, 6), 3, 3);
        p.drawRoundedRect(QRectF(8.5, 7, 9, 6), 3, 3);
        break;
    case Id::Nest:
        p.drawRect(QRectF(3, 4, 14, 12));
        p.drawRect(QRectF(6, 7, 8, 6));
        break;
    case Id::Lock:
        p.drawArc(QRectF(6.5, 3, 7, 8), 0, 180 * 16);
        p.drawLine(QPointF(6.5, 7), QPointF(6.5, 9));
        p.drawLine(QPointF(13.5, 7), QPointF(13.5, 9));
        fill([] { QPainterPath r; r.addRoundedRect(QRectF(5, 9, 10, 8), 1.4, 1.4); return r; }());
        break;
    case Id::Unlock:
        p.drawArc(QRectF(6.5, 3, 7, 8), 0, 180 * 16);
        p.drawLine(QPointF(13.5, 7), QPointF(13.5, 9));
        p.drawRoundedRect(QRectF(5, 9, 10, 8), 1.4, 1.4);
        break;
    case Id::SyncLock:
    case Id::SyncLockOff:
        p.drawRoundedRect(QRectF(2.5, 7.5, 8.5, 5.5), 2.7, 2.7);
        p.drawRoundedRect(QRectF(9, 7.5, 8.5, 5.5), 2.7, 2.7);
        if (id == Id::SyncLockOff) {
            p.setPen(QPen(QColor(0xd9, 0x5b, 0x55), 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(4, 16), QPointF(16, 4));
        }
        break;
    case Id::Eye:
    case Id::EyeOff: {
        QPainterPath eye;
        eye.moveTo(2, 10);
        eye.quadTo(10, 2, 18, 10);
        eye.quadTo(10, 18, 2, 10);
        p.drawPath(eye);
        if (id == Id::Eye) fill([] { QPainterPath r; r.addEllipse(QPointF(10, 10), 2.6, 2.6); return r; }());
        else p.drawLine(QPointF(4, 16), QPointF(16, 4));
        break;
    }
    case Id::Keyframe:
        fill(poly({{10, 3.5}, {16.5, 10}, {10, 16.5}, {3.5, 10}}));
        break;
    case Id::PrevKeyframe:
        fill(poly({{5, 10}, {14, 4.5}, {14, 15.5}}));
        break;
    case Id::NextKeyframe:
        fill(poly({{15, 10}, {6, 4.5}, {6, 15.5}}));
        break;
    case Id::Stopwatch:
    case Id::StopwatchOn:
        p.drawEllipse(QPointF(10, 11), 6, 6);
        p.drawLine(QPointF(10, 5), QPointF(10, 3));
        p.drawLine(QPointF(8, 2.5), QPointF(12, 2.5));
        p.drawLine(QPointF(10, 11), QPointF(10, 7.5));
        p.drawLine(QPointF(10, 11), QPointF(12.5, 11));
        if (id == Id::StopwatchOn) {
            p.save();
            p.setPen(Qt::NoPen);
            QColor bg = c;
            bg.setAlpha(80);
            p.setBrush(bg);
            p.drawEllipse(QPointF(10, 11), 5.2, 5.2);
            p.restore();
        }
        break;
    case Id::Reset: {
        p.drawArc(QRectF(4, 4, 12, 12), 100 * 16, 290 * 16);
        fill(poly({{3, 3}, {8.5, 3.2}, {5.2, 7.8}}));
        break;
    }
    case Id::FxOn:
    case Id::FxOff: {
        QFont f(QStringLiteral("Segoe UI"));
        f.setPixelSize(11);
        f.setItalic(true);
        f.setBold(true);
        QPainterPath t;
        t.addText(QPointF(3.2, 14), f, QStringLiteral("fx"));
        fill(t);
        p.drawRoundedRect(QRectF(1.5, 3.5, 17, 13), 2, 2);
        if (id == Id::FxOff) p.drawLine(QPointF(3, 15), QPointF(17, 5));
        break;
    }
    case Id::TwirlOpen:
        fill(poly({{5, 7.5}, {15, 7.5}, {10, 13.5}}));
        break;
    case Id::TwirlClosed:
        fill(poly({{7.5, 5}, {13.5, 10}, {7.5, 15}}));
        break;

    // --------------------------------------------------------- panels
    case Id::Hamburger:
        p.drawLine(QPointF(4, 6), QPointF(16, 6));
        p.drawLine(QPointF(4, 10), QPointF(16, 10));
        p.drawLine(QPointF(4, 14), QPointF(16, 14));
        break;
    case Id::ListView:
        for (double y : {5.5, 10.0, 14.5}) {
            p.drawLine(QPointF(7, y), QPointF(17, y));
            fill([y] { QPainterPath r; r.addRect(QRectF(3, y - 1, 2, 2)); return r; }());
        }
        break;
    case Id::IconView:
        p.drawRect(QRectF(3, 3, 6, 6));
        p.drawRect(QRectF(11, 3, 6, 6));
        p.drawRect(QRectF(3, 11, 6, 6));
        p.drawRect(QRectF(11, 11, 6, 6));
        break;
    case Id::NewItem: {
        QPainterPath doc;
        doc.moveTo(5, 2.5);
        doc.lineTo(12, 2.5);
        doc.lineTo(15.5, 6);
        doc.lineTo(15.5, 17.5);
        doc.lineTo(5, 17.5);
        doc.closeSubpath();
        p.drawPath(doc);
        p.drawLine(QPointF(12, 2.5), QPointF(12, 6));
        p.drawLine(QPointF(12, 6), QPointF(15.5, 6));
        p.setPen(thin);
        p.drawLine(QPointF(10.2, 9), QPointF(10.2, 15));
        p.drawLine(QPointF(7.2, 12), QPointF(13.2, 12));
        break;
    }
    case Id::NewBin:
    case Id::Folder: {
        QPainterPath folder;
        folder.moveTo(2.5, 5);
        folder.lineTo(8, 5);
        folder.lineTo(9.5, 7);
        folder.lineTo(17.5, 7);
        folder.lineTo(17.5, 16);
        folder.lineTo(2.5, 16);
        folder.closeSubpath();
        p.drawPath(folder);
        if (id == Id::NewBin) {
            p.setPen(thin);
            p.drawLine(QPointF(10, 9), QPointF(10, 14));
            p.drawLine(QPointF(7.5, 11.5), QPointF(12.5, 11.5));
        }
        break;
    }
    case Id::Trash:
        p.drawLine(QPointF(3.5, 5.5), QPointF(16.5, 5.5));
        p.drawLine(QPointF(8, 5.5), QPointF(8.5, 3));
        p.drawLine(QPointF(8.5, 3), QPointF(11.5, 3));
        p.drawLine(QPointF(11.5, 3), QPointF(12, 5.5));
        p.drawLine(QPointF(5, 5.5), QPointF(6, 17));
        p.drawLine(QPointF(6, 17), QPointF(14, 17));
        p.drawLine(QPointF(14, 17), QPointF(15, 5.5));
        p.setPen(thin);
        p.drawLine(QPointF(8.5, 8.5), QPointF(8.8, 14.5));
        p.drawLine(QPointF(11.5, 8.5), QPointF(11.2, 14.5));
        break;
    case Id::Home:
        p.drawLine(QPointF(3, 9.5), QPointF(10, 3));
        p.drawLine(QPointF(10, 3), QPointF(17, 9.5));
        p.drawLine(QPointF(5, 8), QPointF(5, 17));
        p.drawLine(QPointF(15, 8), QPointF(15, 17));
        p.drawLine(QPointF(5, 17), QPointF(15, 17));
        p.drawRect(QRectF(8.5, 11.5, 3, 5.5));
        break;
    case Id::Import:
        p.drawLine(QPointF(10, 2.5), QPointF(10, 11));
        fill(poly({{10, 13.5}, {6, 9}, {14, 9}}));
        p.drawLine(QPointF(3.5, 12), QPointF(3.5, 17));
        p.drawLine(QPointF(3.5, 17), QPointF(16.5, 17));
        p.drawLine(QPointF(16.5, 17), QPointF(16.5, 12));
        break;
    case Id::Export:
        p.drawLine(QPointF(10, 13), QPointF(10, 5));
        fill(poly({{10, 2.5}, {6, 7}, {14, 7}}));
        p.drawLine(QPointF(3.5, 12), QPointF(3.5, 17));
        p.drawLine(QPointF(3.5, 17), QPointF(16.5, 17));
        p.drawLine(QPointF(16.5, 17), QPointF(16.5, 12));
        break;
    case Id::Undo: {
        QPainterPath path;
        path.moveTo(5, 8);
        path.lineTo(12, 8);
        path.quadTo(16.5, 8, 16.5, 12);
        path.quadTo(16.5, 16, 12, 16);
        path.lineTo(8, 16);
        p.drawPath(path);
        fill(poly({{2, 8}, {6.5, 4.5}, {6.5, 11.5}}));
        break;
    }
    case Id::Redo: {
        QPainterPath path;
        path.moveTo(15, 8);
        path.lineTo(8, 8);
        path.quadTo(3.5, 8, 3.5, 12);
        path.quadTo(3.5, 16, 8, 16);
        path.lineTo(12, 16);
        p.drawPath(path);
        fill(poly({{18, 8}, {13.5, 4.5}, {13.5, 11.5}}));
        break;
    }
    case Id::Proxy:
        p.drawRect(QRectF(2.5, 4.5, 15, 11));
        p.setPen(thin);
        p.drawRect(QRectF(8.5, 8.5, 6, 4.5));
        p.drawLine(QPointF(4.5, 6.5), QPointF(8.2, 8.3));
        break;
    case Id::Close:
        p.drawLine(QPointF(5, 5), QPointF(15, 15));
        p.drawLine(QPointF(15, 5), QPointF(5, 15));
        break;
    case Id::Maximize:
        p.drawRect(QRectF(4, 4, 12, 12));
        p.drawLine(QPointF(4, 6.5), QPointF(16, 6.5));
        break;
    case Id::Sequence:
        p.drawRoundedRect(QRectF(2.5, 4, 15, 12), 1.5, 1.5);
        p.setPen(thin);
        p.drawLine(QPointF(4.5, 8), QPointF(11, 8));
        p.drawLine(QPointF(7, 11.5), QPointF(15.5, 11.5));
        break;
    case Id::Film:
        p.drawRect(QRectF(3.5, 3, 13, 14));
        p.drawLine(QPointF(6.5, 3), QPointF(6.5, 17));
        p.drawLine(QPointF(13.5, 3), QPointF(13.5, 17));
        p.setPen(thin);
        for (double y : {6.0, 10.0, 14.0}) {
            p.drawLine(QPointF(3.5, y), QPointF(6.5, y));
            p.drawLine(QPointF(13.5, y), QPointF(16.5, y));
        }
        break;
    case Id::Audio:
        fill(poly({{3, 7.5}, {7, 7.5}, {11, 4}, {11, 16}, {7, 12.5}, {3, 12.5}}));
        p.drawArc(QRectF(9.5, 6, 6, 8), -60 * 16, 120 * 16);
        p.drawArc(QRectF(9.5, 3.5, 9, 13), -55 * 16, 110 * 16);
        break;
    case Id::Still:
        p.drawRect(QRectF(2.5, 4, 15, 12));
        fill(poly({{4, 14.5}, {8.5, 9}, {11.5, 12.5}, {13.5, 10.5}, {16, 14.5}}));
        p.drawEllipse(QPointF(13.5, 7.3), 1.3, 1.3);
        break;
    case Id::Title:
        p.drawRect(QRectF(2.5, 4, 15, 12));
        p.setPen(thin);
        p.drawLine(QPointF(6.5, 7), QPointF(13.5, 7));
        p.drawLine(QPointF(10, 7), QPointF(10, 13.5));
        break;
    case Id::Matte:
        fill([] { QPainterPath r; r.addRoundedRect(QRectF(3, 4, 14, 12), 1.5, 1.5); return r; }());
        break;
    }
}

QHash<quint64, QIcon>& cache()
{
    static QHash<quint64, QIcon> c;
    return c;
}

} // namespace

void paint(QPainter& painter, Id id, const QRectF& rect, const QColor& color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / 20.0, rect.height() / 20.0);
    draw(painter, id, color);
    painter.restore();
}

QPixmap pixmap(Id id, int size, const QColor& color, qreal dpr)
{
    QPixmap px(static_cast<int>(size * dpr), static_cast<int>(size * dpr));
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);
    QPainter p(&px);
    paint(p, id, QRectF(0, 0, size, size), color);
    p.end();
    return px;
}

QIcon icon(Id id)
{
    const quint64 key = static_cast<quint64>(id);
    auto it = cache().find(key);
    if (it != cache().end()) return it.value();

    QIcon ic;
    const QColor normal(0xb4, 0xb4, 0xb8);
    const QColor hover(0xff, 0xff, 0xff);
    const QColor on(0x3b, 0x9b, 0xff);
    const QColor disabled(0x5c, 0x5c, 0x62);
    for (int size : {16, 20, 24, 32}) {
        ic.addPixmap(pixmap(id, size, normal), QIcon::Normal, QIcon::Off);
        ic.addPixmap(pixmap(id, size, hover), QIcon::Active, QIcon::Off);
        ic.addPixmap(pixmap(id, size, on), QIcon::Normal, QIcon::On);
        ic.addPixmap(pixmap(id, size, on), QIcon::Active, QIcon::On);
        ic.addPixmap(pixmap(id, size, on), QIcon::Selected, QIcon::On);
        ic.addPixmap(pixmap(id, size, disabled), QIcon::Disabled, QIcon::Off);
    }
    cache().insert(key, ic);
    return ic;
}

QIcon select() { return icon(Id::Selection); }
QIcon razor() { return icon(Id::Razor); }
QIcon split() { return icon(Id::Razor); }
QIcon remove() { return icon(Id::Trash); }
QIcon undo() { return icon(Id::Undo); }
QIcon redo() { return icon(Id::Redo); }
QIcon zoomIn() { return icon(Id::Zoom); }
QIcon zoomOut() { return icon(Id::Zoom); }
QIcon fit() { return icon(Id::Maximize); }
QIcon play() { return icon(Id::Play); }
QIcon pause() { return icon(Id::Stop); }
QIcon previous() { return icon(Id::StepBack); }
QIcon next() { return icon(Id::StepForward); }
QIcon import() { return icon(Id::Import); }
QIcon proxy() { return icon(Id::Proxy); }

} // namespace bc::icons
