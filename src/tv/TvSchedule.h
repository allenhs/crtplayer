#pragma once
#include <QString>
#include <QVector>

// Cable TV mode: what a channel is broadcasting at any moment (pure logic, unit-tested).
//
// A channel plays its programmes one after another, around the clock, in a cycle. Where it
// is in the cycle depends only on the wall clock, so every channel is "on" whether or not
// anyone is watching, the guide can say what is on everywhere, and coming back to a
// channel finds it further along, as with real television.

struct TvProgram {
    QString source;          // a local file path, or "jellyfin:///<itemId>#<title>"
    QString title;
    qint64 durationMs = 0;
};

struct TvLineup {
    QVector<TvProgram> programs;   // in their natural order; every duration known (> 0)
    QVector<TvProgram> bumpers;    // optional short clips, one after every programme
    bool shuffle = false;          // a new order every cycle (the same for everyone, every time)
    quint32 seed = 0;              // makes this channel's order and timing its own
};

struct TvSlot {
    bool valid = false;
    TvProgram program;
    bool bumper = false;
    qint64 startMs = 0;      // wall clock (ms since the Unix epoch) when it began
    qint64 endMs = 0;
    qint64 offsetMs = 0;     // how far in it is at the asked moment
};

namespace TvSchedule {
qint64 cycleLengthMs(const TvLineup& l);
// The order of the programmes (indices into l.programs) in cycle number `cycle`.
QVector<int> order(const TvLineup& l, qint64 cycle);
// What is on at wall-clock time wallMs.
TvSlot at(const TvLineup& l, qint64 wallMs);
// Everything on between two moments, in order (the first may have begun before fromMs).
QVector<TvSlot> between(const TvLineup& l, qint64 fromMs, qint64 toMs, int maxSlots = 200);
}
