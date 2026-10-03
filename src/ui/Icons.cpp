#include "Icons.h"
#include "Theme.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

void paintIcon(QPainter& p, const QString& n, const QColor& c)
{
    // 24x24 design grid
    QPen pen(c, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto fillPoly = [&](std::initializer_list<QPointF> pts) {
        QPolygonF poly(pts);
        p.setBrush(c);
        p.setPen(QPen(c, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolygon(poly);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    };
    auto speaker = [&] { fillPoly({{3.5, 9}, {7.5, 9}, {12, 5}, {12, 19}, {7.5, 15}, {3.5, 15}}); };

    if (n == "play") fillPoly({{7.5, 5}, {19, 12}, {7.5, 19}});
    else if (n == "pause") { p.setBrush(c); p.setPen(Qt::NoPen); p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1); p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1); }
    else if (n == "prev") { fillPoly({{20, 6.5}, {13, 12}, {20, 17.5}}); fillPoly({{13, 6.5}, {6, 12}, {13, 17.5}}); p.setBrush(c); p.setPen(Qt::NoPen); p.drawRect(QRectF(3.5, 6.5, 2, 11)); }
    else if (n == "next") { fillPoly({{4, 6.5}, {11, 12}, {4, 17.5}}); fillPoly({{11, 6.5}, {18, 12}, {11, 17.5}}); p.setBrush(c); p.setPen(Qt::NoPen); p.drawRect(QRectF(18.5, 6.5, 2, 11)); }
    else if (n == "stepback" || n == "stepfwd") {
        // film frame with a small arrow: reads as "one frame", distinct from playlist skip
        p.drawRoundedRect(QRectF(4, 5, 16, 14), 2, 2);
        QPen thin(c, 1.2); p.setPen(thin);
        for (double x : {7.0, 17.0}) for (double y : {7.5, 11.0, 14.5}) p.drawLine(QPointF(x - 0.6, y), QPointF(x + 0.6, y));
        if (n == "stepback") fillPoly({{14, 8.5}, {9.5, 12}, {14, 15.5}}); else fillPoly({{10, 8.5}, {14.5, 12}, {10, 15.5}});
    }
    else if (n == "volume") { speaker(); p.drawArc(QRectF(10, 8, 7, 8), -60 * 16, 120 * 16); p.drawArc(QRectF(10, 5, 11, 14), -60 * 16, 120 * 16); }
    else if (n == "mute") { speaker(); p.drawLine(QPointF(15, 9.5), QPointF(20, 14.5)); p.drawLine(QPointF(20, 9.5), QPointF(15, 14.5)); }
    else if (n == "fullscreen") {
        p.drawPolyline(QPolygonF({{4, 9}, {4, 4}, {9, 4}})); p.drawPolyline(QPolygonF({{15, 4}, {20, 4}, {20, 9}}));
        p.drawPolyline(QPolygonF({{20, 15}, {20, 20}, {15, 20}})); p.drawPolyline(QPolygonF({{9, 20}, {4, 20}, {4, 15}}));
    } else if (n == "unfullscreen") {
        p.drawPolyline(QPolygonF({{4, 9}, {9, 9}, {9, 4}})); p.drawPolyline(QPolygonF({{15, 4}, {15, 9}, {20, 9}}));
        p.drawPolyline(QPolygonF({{20, 15}, {15, 15}, {15, 20}})); p.drawPolyline(QPolygonF({{9, 20}, {9, 15}, {4, 15}}));
    } else if (n == "open") {
        QPainterPath path; path.moveTo(3.5, 18.5); path.lineTo(3.5, 6); path.lineTo(9, 6); path.lineTo(11, 8); path.lineTo(20.5, 8); path.lineTo(20.5, 18.5); path.closeSubpath();
        p.drawPath(path);
    } else if (n == "playlist") {
        p.drawLine(QPointF(4, 6.5), QPointF(20, 6.5)); p.drawLine(QPointF(4, 11.5), QPointF(20, 11.5)); p.drawLine(QPointF(4, 16.5), QPointF(12, 16.5));
        fillPoly({{15, 14}, {20, 17}, {15, 20}});
    } else if (n == "tune") {
        p.drawLine(QPointF(4, 7), QPointF(20, 7)); p.drawLine(QPointF(4, 12), QPointF(20, 12)); p.drawLine(QPointF(4, 17), QPointF(20, 17));
        p.setBrush(c); p.drawEllipse(QPointF(9, 7), 2.2, 2.2); p.drawEllipse(QPointF(15.5, 12), 2.2, 2.2); p.drawEllipse(QPointF(7, 17), 2.2, 2.2);
    } else if (n == "camera") {
        QPainterPath path; path.addRoundedRect(QRectF(3.5, 7, 17, 12), 2.5, 2.5); p.drawPath(path);
        p.drawLine(QPointF(8.5, 7), QPointF(10, 4.5)); p.drawLine(QPointF(10, 4.5), QPointF(14, 4.5)); p.drawLine(QPointF(14, 4.5), QPointF(15.5, 7));
        p.drawEllipse(QPointF(12, 13), 3.2, 3.2);
    } else if (n == "crt") {
        QPainterPath path; path.addRoundedRect(QRectF(3, 5, 18, 13), 4, 4); p.drawPath(path);
        p.drawLine(QPointF(9, 21), QPointF(15, 21));
        QPen thin(c, 1.0); p.setPen(thin);
        for (double y = 8; y <= 15.5; y += 2.5) p.drawLine(QPointF(6, y), QPointF(18, y));
    } else if (n == "compare") {
        p.drawRoundedRect(QRectF(3.5, 5, 17, 14), 2, 2);
        p.setBrush(c); p.setPen(Qt::NoPen); p.drawRect(QRectF(12, 5, 8.5, 14));
        p.setPen(pen); p.drawLine(QPointF(12, 3), QPointF(12, 21));
    } else if (n == "subtitles") {
        p.drawRoundedRect(QRectF(3, 5.5, 18, 13), 2.5, 2.5);
        p.drawLine(QPointF(6.5, 11), QPointF(10.5, 11)); p.drawLine(QPointF(13, 11), QPointF(17.5, 11));
        p.drawLine(QPointF(6.5, 15), QPointF(14, 15));
    } else if (n == "audio") {
        p.drawLine(QPointF(9, 17), QPointF(9, 5)); p.drawLine(QPointF(9, 5), QPointF(19, 3.5)); p.drawLine(QPointF(19, 3.5), QPointF(19, 15));
        p.setBrush(c); p.drawEllipse(QPointF(6.8, 17.3), 2.6, 2.1); p.drawEllipse(QPointF(16.8, 15.3), 2.6, 2.1);
    } else if (n == "aspect") {
        p.drawRoundedRect(QRectF(3, 6, 18, 12), 1.5, 1.5);
        QPen d(c, 1.4, Qt::DashLine); p.setPen(d); p.drawRect(QRectF(7, 8.5, 10, 7));
    } else if (n == "info") {
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p.setBrush(c); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(12, 7.8), 1.3, 1.3);
    } else if (n == "back") {
        p.drawLine(QPointF(19, 12), QPointF(5.5, 12));
        p.drawPolyline(QPolygonF({{11, 6.5}, {5.5, 12}, {11, 17.5}}));
    } else if (n == "home") {
        p.drawPolyline(QPolygonF({{3.5, 11.5}, {12, 4}, {20.5, 11.5}}));
        p.drawPolyline(QPolygonF({{6, 10}, {6, 20}, {18, 20}, {18, 10}}));
        p.drawRect(QRectF(10, 14, 4, 6));
    } else if (n == "server") {
        for (double y : {4.0, 10.0, 16.0}) {
            p.drawRoundedRect(QRectF(4, y, 16, 4.5), 1.5, 1.5);
            p.setBrush(c); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(16.5, y + 2.25), 0.9, 0.9); p.setPen(pen); p.setBrush(Qt::NoBrush);
        }
    } else if (n == "add") {
        p.drawLine(QPointF(12, 5), QPointF(12, 19)); p.drawLine(QPointF(5, 12), QPointF(19, 12));
    } else if (n == "remove") {
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
    } else if (n == "clear") {
        p.drawLine(QPointF(7, 7), QPointF(17, 17)); p.drawLine(QPointF(17, 7), QPointF(7, 17));
    } else if (n == "shuffle") {   // two crossing paths, arrows at the right
        p.drawPolyline(QPolygonF({{3.5, 7.5}, {7.5, 7.5}, {14, 16.5}, {18, 16.5}}));
        p.drawPolyline(QPolygonF({{3.5, 16.5}, {7.5, 16.5}, {9.3, 14}}));
        p.drawPolyline(QPolygonF({{12.2, 10}, {14, 7.5}, {18, 7.5}}));
        fillPoly({{17.5, 5}, {21, 7.5}, {17.5, 10}});
        fillPoly({{17.5, 14}, {21, 16.5}, {17.5, 19}});
    } else if (n == "repeat" || n == "repeat1") {   // a loop of two arrows; "1": this one only
        p.drawPolyline(QPolygonF({{4.5, 11.5}, {4.5, 7.5}, {16.5, 7.5}}));
        fillPoly({{16, 5}, {19.5, 7.5}, {16, 10}});
        p.drawPolyline(QPolygonF({{19.5, 12.5}, {19.5, 16.5}, {7.5, 16.5}}));
        fillPoly({{8, 14}, {4.5, 16.5}, {8, 19}});
        if (n == "repeat1") {
            p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF({{11, 10.8}, {12.3, 10}, {12.3, 14}}));
        }
    } else if (n == "save") {   // a floppy disk
        p.drawPolygon(QPolygonF({{5, 5}, {16.5, 5}, {19, 7.5}, {19, 19}, {5, 19}}));
        p.drawRect(QRectF(8.5, 5, 6, 4));
        p.drawRect(QRectF(8, 13, 8, 6));
    }
}

} // namespace

QIcon Icons::get(const QString& name)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return *it;
    QIcon icon;
    const struct { QIcon::Mode mode; QColor color; } variants[] = {
        {QIcon::Normal, Theme::text()},
        {QIcon::Active, Theme::accent()},
        {QIcon::Selected, Theme::accent()},
        {QIcon::Disabled, Theme::muted()},
    };
    for (int size : {24, 48, 72}) {
        for (const auto& v : variants) {
            QPixmap pm(size, size);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(size / 24.0, size / 24.0);
            paintIcon(p, name, v.color);
            p.end();
            icon.addPixmap(pm, v.mode, QIcon::Off);
            if (v.mode == QIcon::Normal) {
                // Checked state (e.g. active toggles) uses the accent colour.
                QPixmap on(size, size);
                on.fill(Qt::transparent);
                QPainter q(&on);
                q.setRenderHint(QPainter::Antialiasing);
                q.scale(size / 24.0, size / 24.0);
                paintIcon(q, name, Theme::accent());
                q.end();
                icon.addPixmap(on, QIcon::Normal, QIcon::On);
                icon.addPixmap(on, QIcon::Active, QIcon::On);
            }
        }
    }
    cache.insert(name, icon);
    return icon;
}
