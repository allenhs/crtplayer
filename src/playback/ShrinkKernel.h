#pragma once
// Shrinking a picture plane by a whole factor and to 8 bits a sample, in one pass over it (2.17).
//
// Without a graphics card a 4K video is shown much smaller than it is, and GStreamer's scaler is
// slow on the 16-bit samples of a 10-bit video (measured: scaling and converting 4K HEVC 10-bit
// took longer than decoding it). Averaging whole blocks of k × k samples needs no filter tables:
// a 4K 10-bit frame becomes a 1080p 8-bit one in under 2 ms on one core. What is left for the
// scaler is a small step on 8-bit samples.
//
// Every source sample is averaged in (no detail is skipped, so nothing turns jagged or noisy),
// and the bits dropped are spread with an ordered dither, so that smooth gradients do not band.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Shrink {

// The 4 × 4 ordered-dither matrix: thresholds 0..15, read as (t + 0) / 16 of one output step.
inline int bayer4(int x, int y)
{
    static const uint8_t m[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    return m[y & 3][x & 3];
}

// Rows [rowBegin, rowEnd) of the destination plane (dw × dh, 8 bits a sample) from the source plane
// (sw × sh, samples of type T holding `depth` bits). dw = ceil(sw / k), dh = ceil(sh / k): blocks at
// the right and bottom edges may be narrower. Strides are in bytes.
template <typename T>
void plane(const uint8_t* src, int sstride, int sw, int sh, int depth,
           uint8_t* dst, int dstride, int dw, int dh, int k, int rowBegin, int rowEnd)
{
    const int shift = depth - 8;
    const int full = sw / k;                    // blocks with all k columns
    const int rest = sw - full * k;             // columns of the narrower block at the right edge, if any
    rowEnd = std::min(rowEnd, dh);
    // Dither thresholds for the two fast cases: a pattern row for each of the four row phases.
    const int fastShift = k == 1 ? shift : shift + 2;
    std::vector<uint16_t> pat;
    if (k <= 2) {
        pat.resize(size_t(4) * size_t(dw));
        for (int p = 0; p < 4; ++p)
            for (int x = 0; x < dw; ++x) pat[size_t(p) * dw + x] = uint16_t((bayer4(x, p) << fastShift) >> 4);
    }
    std::vector<uint32_t> acc;
    if (k > 1) acc.resize(size_t(dw));
    for (int r = rowBegin; r < rowEnd; ++r) {
        uint8_t* d = dst + size_t(r) * dstride;
        const int y0 = r * k;
        const int ny = std::min(k, sh - y0);
        const T* a = reinterpret_cast<const T*>(src + size_t(y0) * sstride);
        if (k == 1) {
            if (shift == 0) { std::memcpy(d, a, size_t(dw)); continue; }
            const uint16_t* th = &pat[size_t(r & 3) * dw];
            for (int x = 0; x < dw; ++x) {
                const unsigned v = (unsigned(a[x]) + th[x]) >> shift;
                d[x] = uint8_t(v > 255u ? 255u : v);
            }
            continue;
        }
        if (k == 2 && ny == 2) {
            const T* b = reinterpret_cast<const T*>(src + size_t(y0 + 1) * sstride);
            const uint16_t* th = &pat[size_t(r & 3) * dw];
            for (int x = 0; x < full; ++x) {
                const unsigned v = (unsigned(a[2 * x]) + a[2 * x + 1] + b[2 * x] + b[2 * x + 1] + th[x]) >> fastShift;
                d[x] = uint8_t(v > 255u ? 255u : v);
            }
            if (rest) {   // one column left
                const unsigned v = (2u * a[2 * full] + 2u * b[2 * full] + th[full]) >> fastShift;
                d[full] = uint8_t(v > 255u ? 255u : v);
            }
            continue;
        }
        // Any factor, and the narrower blocks at the edges: sum, then divide by the block's size.
        std::fill(acc.begin(), acc.end(), 0u);
        for (int j = 0; j < ny; ++j) {
            const T* s = reinterpret_cast<const T*>(src + size_t(y0 + j) * sstride);
            if (k == 3) { for (int x = 0; x < full; ++x) acc[x] += unsigned(s[3 * x]) + s[3 * x + 1] + s[3 * x + 2]; }
            else if (k == 4) { for (int x = 0; x < full; ++x) acc[x] += unsigned(s[4 * x]) + s[4 * x + 1] + s[4 * x + 2] + s[4 * x + 3]; }
            else {
                for (int x = 0; x < full; ++x) {
                    unsigned t = 0;
                    for (int i = 0; i < k; ++i) t += s[k * x + i];
                    acc[x] += t;
                }
            }
            for (int i = 0; i < rest; ++i) acc[full] += s[k * full + i];
        }
        // value = floor(sum / (count << shift) + t / 16), by a multiplication (sums stay far below 2^24)
        auto finish = [&](int x0, int x1, int count) {
            const uint64_t div16 = uint64_t(16) * (uint64_t(count) << shift);
            const uint64_t recip = (uint64_t(1) << 40) / div16 + 1;
            const uint64_t step = uint64_t(count) << shift;
            for (int x = x0; x < x1; ++x) {
                const uint64_t num = uint64_t(acc[x]) * 16 + uint64_t(bayer4(x, r)) * step;
                const uint64_t v = (num * recip) >> 40;
                d[x] = uint8_t(v > 255u ? 255u : v);
            }
        };
        finish(0, full, ny * k);
        if (rest) finish(full, full + 1, ny * rest);
    }
}

} // namespace Shrink
