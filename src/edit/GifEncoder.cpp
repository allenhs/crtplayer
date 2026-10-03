#include "GifEncoder.h"

#include <QMutexLocker>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

// ---- palette: median cut over a 5-bit-per-channel histogram -------------------------------

struct Box { int r0, r1, g0, g1, b0, b1; quint64 count; };

inline int key15(int r, int g, int b) { return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); }

void shrink(Box& bx, const std::vector<quint32>& hist)
{
    int r0 = 32, r1 = -1, g0 = 32, g1 = -1, b0 = 32, b1 = -1;
    quint64 n = 0;
    for (int r = bx.r0; r <= bx.r1; ++r)
        for (int g = bx.g0; g <= bx.g1; ++g)
            for (int b = bx.b0; b <= bx.b1; ++b) {
                const quint32 c = hist[(r << 10) | (g << 5) | b];
                if (!c) continue;
                n += c;
                r0 = std::min(r0, r); r1 = std::max(r1, r);
                g0 = std::min(g0, g); g1 = std::max(g1, g);
                b0 = std::min(b0, b); b1 = std::max(b1, b);
            }
    bx.count = n;
    if (n) { bx.r0 = r0; bx.r1 = r1; bx.g0 = g0; bx.g1 = g1; bx.b0 = b0; bx.b1 = b1; }
}

std::vector<QRgb> medianCut(const std::vector<quint32>& hist, int maxColors)
{
    std::vector<Box> boxes{{0, 31, 0, 31, 0, 31, 0}};
    shrink(boxes[0], hist);
    while (int(boxes.size()) < maxColors) {
        // The box with the most pixels times its longest side is split.
        int best = -1;
        double bestScore = 0;
        for (int i = 0; i < int(boxes.size()); ++i) {
            const Box& b = boxes[i];
            const int side = std::max({b.r1 - b.r0, b.g1 - b.g0, b.b1 - b.b0});
            if (side <= 0) continue;
            const double score = double(b.count) * side;
            if (score > bestScore) { bestScore = score; best = i; }
        }
        if (best < 0) break;
        Box b = boxes[best];
        const int dr = b.r1 - b.r0, dg = b.g1 - b.g0, db = b.b1 - b.b0;
        const int axis = (dg >= dr && dg >= db) ? 1 : (dr >= db ? 0 : 2);
        // Split at the median along that axis.
        const int lo = axis == 0 ? b.r0 : axis == 1 ? b.g0 : b.b0;
        const int hi = axis == 0 ? b.r1 : axis == 1 ? b.g1 : b.b1;
        std::vector<quint64> plane(32, 0);
        for (int r = b.r0; r <= b.r1; ++r)
            for (int g = b.g0; g <= b.g1; ++g)
                for (int bb = b.b0; bb <= b.b1; ++bb)
                    plane[axis == 0 ? r : axis == 1 ? g : bb] += hist[(r << 10) | (g << 5) | bb];
        quint64 acc = 0;
        int cut = lo;
        for (int v = lo; v < hi; ++v) {
            acc += plane[v];
            cut = v;
            if (acc * 2 >= b.count) break;
        }
        Box a = b, c = b;
        if (axis == 0) { a.r1 = cut; c.r0 = cut + 1; }
        else if (axis == 1) { a.g1 = cut; c.g0 = cut + 1; }
        else { a.b1 = cut; c.b0 = cut + 1; }
        shrink(a, hist);
        shrink(c, hist);
        boxes[best] = a;
        if (c.count) boxes.push_back(c);
        if (!a.count) boxes.erase(boxes.begin() + best);
    }
    std::vector<QRgb> pal;
    for (const Box& bx : boxes) {
        double sr = 0, sg = 0, sb = 0, n = 0;
        for (int r = bx.r0; r <= bx.r1; ++r)
            for (int g = bx.g0; g <= bx.g1; ++g)
                for (int b = bx.b0; b <= bx.b1; ++b) {
                    const quint32 c = hist[(r << 10) | (g << 5) | b];
                    if (!c) continue;
                    sr += c * (r * 8 + 4); sg += c * (g * 8 + 4); sb += c * (b * 8 + 4); n += c;
                }
        if (n > 0) pal.push_back(qRgb(int(sr / n), int(sg / n), int(sb / n)));
    }
    if (pal.empty()) pal.push_back(qRgb(0, 0, 0));
    return pal;
}

// ---- LZW (GIF flavour) --------------------------------------------------------------------

class BitWriter {
public:
    void put(int code, int bits)
    {
        m_acc |= quint32(code) << m_n;
        m_n += bits;
        while (m_n >= 8) { m_bytes.push_back(char(m_acc & 0xff)); m_acc >>= 8; m_n -= 8; }
    }
    QByteArray finish()
    {
        if (m_n > 0) m_bytes.push_back(char(m_acc & 0xff));
        m_acc = 0;
        m_n = 0;
        return m_bytes;
    }
private:
    QByteArray m_bytes;
    quint32 m_acc = 0;
    int m_n = 0;
};

QByteArray lzw(const std::vector<quint8>& px, int minCodeSize)
{
    const int clear = 1 << minCodeSize, eoi = clear + 1;
    BitWriter out;
    // Dictionary: (prefix code, byte) -> code, as a hash table.
    constexpr int kTable = 5003 * 4;
    std::vector<qint32> keys(kTable, -1);
    std::vector<qint16> vals(kTable, 0);
    int codeSize = minCodeSize + 1, next = eoi + 1;
    auto reset = [&] { std::fill(keys.begin(), keys.end(), -1); codeSize = minCodeSize + 1; next = eoi + 1; };
    out.put(clear, codeSize);
    if (px.empty()) { out.put(eoi, codeSize); return out.finish(); }
    int prefix = px[0];
    for (size_t i = 1; i < px.size(); ++i) {
        const int c = px[i];
        const qint32 k = (prefix << 8) | c;
        int h = int((quint32(k) * 2654435761u) % kTable);
        int found = -1;
        while (keys[h] != -1) {
            if (keys[h] == k) { found = vals[h]; break; }
            h = (h + 1) % kTable;
        }
        if (found >= 0) { prefix = found; continue; }
        out.put(prefix, codeSize);
        if (next < 4096) {
            keys[h] = k;
            vals[h] = qint16(next);
            if (next == (1 << codeSize) && codeSize < 12) ++codeSize;
            ++next;
        } else {
            out.put(clear, codeSize);
            reset();
        }
        prefix = c;
    }
    out.put(prefix, codeSize);
    // (the decoder grows the code size one code later than the encoder assigns it)
    out.put(eoi, codeSize);
    return out.finish();
}

void putShort(QByteArray& b, int v) { b.push_back(char(v & 0xff)); b.push_back(char((v >> 8) & 0xff)); }

} // namespace

QByteArray GifEncoder::encodeFrame(const QImage& in, int delayCs, bool dither)
{
    const QImage img = in.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    std::vector<quint32> hist(32768, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) ++hist[key15(qRed(row[x]), qGreen(row[x]), qBlue(row[x]))];
    }
    const std::vector<QRgb> pal = medianCut(hist, 256);
    // Nearest palette entry for a 15-bit colour, worked out when first needed.
    std::vector<qint16> nearest(32768, -1);
    auto lookup = [&](int r, int g, int b) {
        const int k = key15(r, g, b);
        if (nearest[k] < 0) {
            const int rr = (r & ~7) + 4, gg = (g & ~7) + 4, bb = (b & ~7) + 4;
            int best = 0, bestD = INT32_MAX;
            for (int i = 0; i < int(pal.size()); ++i) {
                const int dr = qRed(pal[i]) - rr, dg = qGreen(pal[i]) - gg, db = qBlue(pal[i]) - bb;
                const int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
                if (d < bestD) { bestD = d; best = i; }
            }
            nearest[k] = qint16(best);
        }
        return int(nearest[k]);
    };
    std::vector<quint8> idx(size_t(w) * h);
    if (dither) {
        // Floyd–Steinberg, serpentine, at 3/4 strength (full strength shimmers from frame to frame).
        std::vector<float> err0(size_t(w + 2) * 3, 0.f), err1(size_t(w + 2) * 3, 0.f);
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            std::fill(err1.begin(), err1.end(), 0.f);
            const bool ltr = (y % 2) == 0;
            for (int i = 0; i < w; ++i) {
                const int x = ltr ? i : w - 1 - i;
                float* e = &err0[size_t(x + 1) * 3];
                const int r = std::clamp(int(std::lround(qRed(row[x]) + e[0])), 0, 255);
                const int g = std::clamp(int(std::lround(qGreen(row[x]) + e[1])), 0, 255);
                const int b = std::clamp(int(std::lround(qBlue(row[x]) + e[2])), 0, 255);
                const int p = lookup(r, g, b);
                idx[size_t(y) * w + x] = quint8(p);
                const float er[3] = {0.75f * (r - qRed(pal[p])), 0.75f * (g - qGreen(pal[p])), 0.75f * (b - qBlue(pal[p]))};
                const int dx = ltr ? 1 : -1;
                for (int ch = 0; ch < 3; ++ch) {
                    err0[size_t(x + 1 + dx) * 3 + ch] += er[ch] * 7.f / 16.f;
                    err1[size_t(x + 1 - dx) * 3 + ch] += er[ch] * 3.f / 16.f;
                    err1[size_t(x + 1) * 3 + ch] += er[ch] * 5.f / 16.f;
                    err1[size_t(x + 1 + dx) * 3 + ch] += er[ch] * 1.f / 16.f;
                }
            }
            std::swap(err0, err1);
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) idx[size_t(y) * w + x] = quint8(lookup(qRed(row[x]), qGreen(row[x]), qBlue(row[x])));
        }
    }

    QByteArray out;
    // Graphic control extension: the delay; disposal "leave in place".
    out.append("\x21\xf9\x04", 3);
    out.push_back(char(0x04));
    putShort(out, std::clamp(delayCs, 2, 65535));
    out.push_back(char(0));
    out.push_back(char(0));
    // Image descriptor with a local 256-entry colour table.
    out.push_back(char(0x2c));
    putShort(out, 0);
    putShort(out, 0);
    putShort(out, w);
    putShort(out, h);
    out.push_back(char(0x87));   // local table, 2^(7+1) = 256 entries
    for (int i = 0; i < 256; ++i) {
        const QRgb c = i < int(pal.size()) ? pal[i] : qRgb(0, 0, 0);
        out.push_back(char(qRed(c)));
        out.push_back(char(qGreen(c)));
        out.push_back(char(qBlue(c)));
    }
    out.push_back(char(8));   // LZW minimum code size
    const QByteArray data = lzw(idx, 8);
    for (int i = 0; i < data.size(); i += 255) {
        const int n = std::min(255, int(data.size()) - i);
        out.push_back(char(n));
        out.append(data.constData() + i, n);
    }
    out.push_back(char(0));
    return out;
}

// Shrinks a frame to the GIF's size. Fine regular patterns (scanlines, a shadow mask)
// would turn into moiré rings if simply resampled, so the frame is first softened in
// proportion to the reduction (two box blurs, close to a Gaussian).
QImage GifEncoder::fitTo(const QImage& in, const QSize& size)
{
    if (in.size() == size) return in;
    QImage img = in.convertToFormat(QImage::Format_RGB32);
    const double f = std::min(double(img.width()) / size.width(), double(img.height()) / size.height());
    const int r = f > 1.2 ? int(std::lround(f * 0.5)) : 0;
    if (r > 0) {
        const int w = img.width(), h = img.height();
        std::vector<QRgb> tmp(size_t(std::max(w, h)));
        auto pass = [&](bool horizontal) {
            const int lines = horizontal ? h : w, len = horizontal ? w : h;
            for (int l = 0; l < lines; ++l) {
                auto at = [&](int i) -> QRgb& {
                    return horizontal ? reinterpret_cast<QRgb*>(img.scanLine(l))[i] : reinterpret_cast<QRgb*>(img.scanLine(i))[l];
                };
                int sr = 0, sg = 0, sb = 0, n = 0;
                for (int i = -r; i <= r; ++i) {
                    const QRgb c = at(std::clamp(i, 0, len - 1));
                    sr += qRed(c); sg += qGreen(c); sb += qBlue(c); ++n;
                }
                for (int i = 0; i < len; ++i) {
                    tmp[i] = qRgb(sr / n, sg / n, sb / n);
                    const QRgb out = at(std::clamp(i - r, 0, len - 1)), inn = at(std::clamp(i + r + 1, 0, len - 1));
                    sr += qRed(inn) - qRed(out); sg += qGreen(inn) - qGreen(out); sb += qBlue(inn) - qBlue(out);
                }
                for (int i = 0; i < len; ++i) at(i) = tmp[i];
            }
        };
        for (int k = 0; k < 2; ++k) { pass(true); pass(false); }
    }
    return img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

GifEncoder::~GifEncoder() { abort(); }

bool GifEncoder::open(const QString& path, int width, int height, bool loop)
{
    abort();
    m_file.setFileName(path);
    // Never over an existing file.
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    m_w = width & ~1;
    m_h = height & ~1;
    m_added = m_nextToWrite = 0;
    m_stop = m_writeError = false;
    m_encoded.clear();
    QByteArray h("GIF89a");
    putShort(h, m_w);
    putShort(h, m_h);
    h.push_back(char(0x70));   // no global table, 8-bit colour resolution
    h.push_back(char(0));
    h.push_back(char(0));
    if (loop) {
        h.append("\x21\xff\x0bNETSCAPE2.0\x03\x01", 16);
        putShort(h, 0);   // forever
        h.push_back(char(0));
    }
    m_file.write(h);
    const int n = std::clamp(int(std::thread::hardware_concurrency()) - 1, 1, 6);
    for (int i = 0; i < n; ++i) m_threads.emplace_back([this] { worker(); });
    return true;
}

void GifEncoder::addFrame(const QImage& frame, int delayCs)
{
    if (!m_file.isOpen()) return;
    QMutexLocker l(&m_lock);
    m_jobs.enqueue({m_added++, frame, delayCs});   // scaled on a worker thread
    ++m_pending;
    m_jobReady.wakeOne();
}

void GifEncoder::worker()
{
    for (;;) {
        Job job;
        {
            QMutexLocker l(&m_lock);
            while (m_jobs.isEmpty() && !m_stop) m_jobReady.wait(&m_lock);
            if (m_jobs.isEmpty()) return;
            job = m_jobs.dequeue();
        }
        const QByteArray bytes = encodeFrame(fitTo(job.image, QSize(m_w, m_h)), job.delay);
        QMutexLocker l(&m_lock);
        m_encoded.insert(job.index, bytes);
        // Write whatever is next in order.
        while (m_encoded.contains(m_nextToWrite)) {
            if (m_file.write(m_encoded.take(m_nextToWrite)) < 0) m_writeError = true;
            ++m_nextToWrite;
            --m_pending;
        }
        m_done.wakeAll();
    }
}

bool GifEncoder::close()
{
    if (!m_file.isOpen()) return false;
    {
        QMutexLocker l(&m_lock);
        while (m_nextToWrite < m_added) m_done.wait(&m_lock);
        m_stop = true;
        m_jobReady.wakeAll();
    }
    for (std::thread& t : m_threads) t.join();
    m_threads.clear();
    m_file.write(";", 1);
    const bool ok = !m_writeError && m_file.error() == QFileDevice::NoError;
    m_file.close();
    return ok;
}

void GifEncoder::abort()
{
    {
        QMutexLocker l(&m_lock);
        m_stop = true;
        m_jobs.clear();
        m_jobReady.wakeAll();
    }
    for (std::thread& t : m_threads) t.join();
    m_threads.clear();
    m_pending = 0;
    if (m_file.isOpen()) {
        m_file.close();
        m_file.remove();
    }
}
