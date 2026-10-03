#include "TvSchedule.h"

#include <algorithm>

namespace {
constexpr qint64 kEpochMs = 946684800000LL;   // 2000-01-01 00:00 UTC: where every cycle count starts

quint64 mix(quint64 x)   // splitmix64
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

QVector<int> shuffled(int n, quint32 seed, qint64 cycle)
{
    QVector<int> o(n);
    for (int i = 0; i < n; ++i) o[i] = i;
    quint64 s = mix((quint64(seed) << 32) ^ quint64(cycle));
    for (int i = n - 1; i > 0; --i) {   // Fisher–Yates
        s = mix(s);
        std::swap(o[i], o[int(s % quint64(i + 1))]);
    }
    return o;
}

int bumperFor(const TvLineup& l, int position)
{
    return int((quint64(position) * 7919u + l.seed) % quint64(l.bumpers.size()));
}

qint64 floorDiv(qint64 a, qint64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The phase keeps two channels with the same programmes from running in step.
qint64 phase(const TvLineup& l, qint64 total) { return qint64(mix(l.seed) % quint64(total)); }

struct Item { const TvProgram* p; bool bumper; };

QVector<Item> sequence(const TvLineup& l, qint64 cycle)
{
    QVector<Item> seq;
    const QVector<int> o = TvSchedule::order(l, cycle);
    seq.reserve(o.size() * 2);
    for (int i = 0; i < o.size(); ++i) {
        seq.push_back({&l.programs[o[i]], false});
        if (!l.bumpers.isEmpty()) seq.push_back({&l.bumpers[bumperFor(l, i)], true});
    }
    return seq;
}
} // namespace

qint64 TvSchedule::cycleLengthMs(const TvLineup& l)
{
    qint64 t = 0;
    for (const TvProgram& p : l.programs) t += std::max<qint64>(0, p.durationMs);
    if (!l.bumpers.isEmpty())
        for (int i = 0; i < l.programs.size(); ++i) t += std::max<qint64>(0, l.bumpers[bumperFor(l, i)].durationMs);
    return t;
}

QVector<int> TvSchedule::order(const TvLineup& l, qint64 cycle)
{
    const int n = l.programs.size();
    if (!l.shuffle || n < 2) {
        QVector<int> o(n);
        for (int i = 0; i < n; ++i) o[i] = i;
        return o;
    }
    QVector<int> o = shuffled(n, l.seed, cycle);
    // Never the same programme twice in a row across the turn of a cycle.
    if (n > 2 && o.first() == shuffled(n, l.seed, cycle - 1).last()) std::swap(o[0], o[1]);
    return o;
}

TvSlot TvSchedule::at(const TvLineup& l, qint64 wallMs)
{
    const QVector<TvSlot> s = between(l, wallMs, wallMs + 1, 1);
    return s.isEmpty() ? TvSlot() : s.first();
}

QVector<TvSlot> TvSchedule::between(const TvLineup& l, qint64 fromMs, qint64 toMs, int maxSlots)
{
    QVector<TvSlot> out;
    const qint64 total = cycleLengthMs(l);
    if (total <= 0 || l.programs.isEmpty() || toMs <= fromMs) return out;
    const qint64 rel = fromMs - kEpochMs + phase(l, total);
    qint64 cycle = floorDiv(rel, total);
    qint64 cycleStart = fromMs - (rel - cycle * total);   // wall clock at which this cycle began
    while (out.size() < maxSlots) {
        qint64 t = cycleStart;
        for (const Item& it : sequence(l, cycle)) {
            const qint64 d = std::max<qint64>(0, it.p->durationMs);
            if (d > 0 && t + d > fromMs && t < toMs) {
                TvSlot s;
                s.valid = true;
                s.program = *it.p;
                s.bumper = it.bumper;
                s.startMs = t;
                s.endMs = t + d;
                s.offsetMs = std::clamp<qint64>(fromMs - t, 0, d);
                out.push_back(s);
                if (out.size() >= maxSlots) return out;
            }
            t += d;
            if (t >= toMs) return out;
        }
        cycleStart += total;
        ++cycle;
    }
    return out;
}
