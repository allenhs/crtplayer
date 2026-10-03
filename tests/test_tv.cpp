// TvSchedule: the round-the-clock channel schedule.
#include "tv/TvSchedule.h"
#include <QSet>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }

int main()
{
    TvLineup l;
    const qint64 mins[] = {22, 45, 7, 90, 31, 12, 58};
    for (int i = 0; i < 7; ++i) l.programs.push_back({QStringLiteral("/v/p%1.mkv").arg(i), QStringLiteral("P%1").arg(i), mins[i] * 60000 + i * 137});
    l.seed = 42;
    const qint64 now = 1790000000000LL;   // some moment in 2026
    const qint64 total = TvSchedule::cycleLengthMs(l);
    check(total == (22 + 45 + 7 + 90 + 31 + 12 + 58) * 60000 + (0 + 1 + 2 + 3 + 4 + 5 + 6) * 137, "the cycle is as long as its programmes");

    // Every moment has exactly one programme, and the offset is the time since it began.
    bool consistent = true, sameAgain = true;
    for (qint64 t = now; t < now + 3 * total; t += 97 * 1000) {
        const TvSlot s = TvSchedule::at(l, t);
        if (!s.valid || s.startMs > t || s.endMs <= t || s.offsetMs != t - s.startMs || s.endMs - s.startMs != s.program.durationMs) consistent = false;
        const TvSlot again = TvSchedule::at(l, t);
        if (again.program.source != s.program.source || again.startMs != s.startMs) sameAgain = false;
    }
    check(consistent, "at any moment: one programme, started before, ends after, offset = time since its start");
    check(sameAgain, "asking again gives the same answer");

    // In order: the natural order, back to back, cycle after cycle.
    QVector<TvSlot> day = TvSchedule::between(l, now, now + 2 * total, 1000);
    bool backToBack = true, inOrder = true;
    for (int i = 1; i < day.size(); ++i) {
        if (day[i].startMs != day[i - 1].endMs) backToBack = false;
        const int a = day[i - 1].program.title.mid(1).toInt(), b = day[i].program.title.mid(1).toInt();
        if (b != (a + 1) % 7) inOrder = false;
    }
    check(day.size() >= 14 && backToBack, "programmes follow one another without gaps or overlaps");
    check(inOrder, "in order: each programme is followed by the next, and the last by the first");
    check(TvSchedule::at(l, day[3].endMs).program.source == day[4].program.source && TvSchedule::at(l, day[3].endMs).offsetMs == 0,
          "at the moment one ends, the next is at its very start");
    check(TvSchedule::at(l, day[3].endMs - 1).program.source == day[3].program.source, "a millisecond before, the first is still on");

    // Shuffled: every programme once per cycle, a different order each cycle, the same every time asked.
    l.shuffle = true;
    const QVector<int> c0 = TvSchedule::order(l, 9000), c1 = TvSchedule::order(l, 9001);
    QSet<int> seen(c0.begin(), c0.end());
    check(c0.size() == 7 && seen.size() == 7, "shuffled: every programme exactly once per cycle");
    check(c0 != c1, "shuffled: a different order in the next cycle");
    check(c0 == TvSchedule::order(l, 9000), "shuffled: the same order whenever that cycle is asked for");
    bool noRepeat = true;
    for (qint64 c = 100; c < 400; ++c)
        if (TvSchedule::order(l, c).first() == TvSchedule::order(l, c - 1).last()) noRepeat = false;
    check(noRepeat, "shuffled: never the same programme twice in a row across cycles");
    day = TvSchedule::between(l, now, now + 2 * total, 1000);
    backToBack = true;
    for (int i = 1; i < day.size(); ++i) if (day[i].startMs != day[i - 1].endMs) backToBack = false;
    check(backToBack && TvSchedule::cycleLengthMs(l) == total, "shuffled: still back to back, same cycle length");
    TvLineup other = l;
    other.seed = 43;
    check(TvSchedule::at(other, now).startMs != TvSchedule::at(l, now).startMs || TvSchedule::at(other, now).program.source != TvSchedule::at(l, now).program.source,
          "two channels with the same programmes are not in step");

    // Bumpers: one after every programme.
    l.shuffle = false;
    l.bumpers = {{"/b/a.mp4", "Ident A", 15000}, {"/b/b.mp4", "Ident B", 30000}, {"/b/c.mp4", "Ident C", 10000}};
    day = TvSchedule::between(l, now, now + TvSchedule::cycleLengthMs(l) * 2, 1000);
    bool alternate = true;
    backToBack = true;
    for (int i = 1; i < day.size(); ++i) {
        if (day[i].bumper == day[i - 1].bumper) alternate = false;
        if (day[i].startMs != day[i - 1].endMs) backToBack = false;
    }
    check(alternate && backToBack, "with bumpers: programme, bumper, programme, bumper, back to back");
    qint64 sum = 0;
    for (const TvSlot& s : TvSchedule::between(l, day[1].startMs, day[1].startMs + TvSchedule::cycleLengthMs(l), 1000)) sum += s.endMs - s.startMs;
    check(sum == TvSchedule::cycleLengthMs(l), "with bumpers: one cycle's slots add up to the cycle length");

    // Guide window: the first slot may have begun earlier; nothing starts at or after the end.
    const QVector<TvSlot> win = TvSchedule::between(l, now, now + 90 * 60000);
    check(!win.isEmpty() && win.first().startMs <= now && win.last().startMs < now + 90 * 60000 && win.last().endMs >= now + 90 * 60000,
          "a 90-minute guide window is covered from its first minute to its last");
    check(TvSchedule::at(TvLineup(), now).valid == false, "an empty channel has nothing on");
    std::printf(fails ? "\n%d TV schedule check(s) failed\n" : "\nAll TV schedule checks passed\n", fails);
    return fails ? 1 : 0;
}
