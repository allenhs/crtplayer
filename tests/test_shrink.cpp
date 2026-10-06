// The player's own shrinking of 10-bit pictures (src/playback/ShrinkKernel.h) and the governor's reading of
// H.264 / H.265 pictures (src/playback/FrameGovernor.cpp).
#include "playback/FrameGovernor.h"
#include "playback/ShrinkKernel.h"

#include <QString>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0;
static void check(const char* name, bool ok, const QString& detail)
{
    std::printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name, detail.toUtf8().constData());
    if (!ok) ++g_fail;
}

// A plane of sw × sh 16-bit samples shrunk by k; returns the 8-bit plane (dw × dh).
static std::vector<uint8_t> shrink16(const std::vector<uint16_t>& src, int sw, int sh, int depth, int k, int* dw, int* dh)
{
    *dw = (sw + k - 1) / k; *dh = (sh + k - 1) / k;
    std::vector<uint8_t> dst(size_t(*dw) * size_t(*dh), 0);
    // (in two bands, as the filter's threads do it)
    Shrink::plane<uint16_t>(reinterpret_cast<const uint8_t*>(src.data()), sw * 2, sw, sh, depth, dst.data(), *dw, *dw, *dh, k, 0, *dh / 2);
    Shrink::plane<uint16_t>(reinterpret_cast<const uint8_t*>(src.data()), sw * 2, sw, sh, depth, dst.data(), *dw, *dw, *dh, k, *dh / 2, *dh);
    return dst;
}

int main()
{
    // ---- shrinking
    for (int k = 1; k <= 5; ++k) {
        // A flat grey: every output sample is that grey in 8 bits, give or take the dither's one step.
        const int sw = 64, sh = 40;
        std::vector<uint16_t> flat(size_t(sw) * sh, 514);   // 514 / 4 = 128.5
        int dw, dh;
        const std::vector<uint8_t> out = shrink16(flat, sw, sh, 10, k, &dw, &dh);
        int lo = 255, hi = 0; double sum = 0;
        for (uint8_t v : out) { lo = std::min<int>(lo, v); hi = std::max<int>(hi, v); sum += v; }
        check("a flat 10-bit grey stays that grey", dw == (sw + k - 1) / k && lo >= 128 && hi <= 129 && std::abs(sum / out.size() - 128.5) < 0.15,
              QString("factor %1: %2 x %3, values %4..%5, mean %6 (128.5 in 10 bits)").arg(k).arg(dw).arg(dh).arg(lo).arg(hi).arg(sum / out.size(), 0, 'f', 3));
    }
    {
        // Every source sample counts: one bright sample in a block raises that block's average, wherever it sits.
        const int sw = 12, sh = 12, k = 3;
        bool all = true;
        for (int y = 0; y < 3 && all; ++y)
            for (int x = 0; x < 3 && all; ++x) {
                std::vector<uint16_t> p(size_t(sw) * sh, 0);
                p[size_t(3 + y) * sw + 3 + x] = 1023;   // inside block (1, 1)
                int dw, dh;
                const std::vector<uint8_t> out = shrink16(p, sw, sh, 10, k, &dw, &dh);
                const int v = out[size_t(1) * dw + 1];   // 1023 / 9 / 4 = 28.4
                all = (v == 28 || v == 29);
                for (int i = 0; i < dw * dh && all; ++i)
                    if (i != dw + 1 && out[size_t(i)] != 0) all = false;
            }
        check("every sample of a block is averaged in, none skipped", all, "one bright sample at each of the 9 places of a 3 x 3 block");
    }
    {
        // Sizes that do not divide: the narrower blocks at the right and bottom edges are averaged over what they hold.
        const int sw = 7, sh = 5, k = 2;
        std::vector<uint16_t> p(size_t(sw) * sh, 400);
        for (int y = 0; y < sh; ++y) p[size_t(y) * sw + 6] = 800;   // the last column
        for (int x = 0; x < sw; ++x) p[size_t(4) * sw + x] = 800;   // the last line
        int dw, dh;
        const std::vector<uint8_t> out = shrink16(p, sw, sh, 10, k, &dw, &dh);
        const int inner = out[0], right = out[size_t(dw) - 1], bottom = out[size_t(dh - 1) * dw], corner = out[size_t(dh) * dw - 1];
        check("edges of a size that does not divide evenly", dw == 4 && dh == 3 && inner >= 100 && inner <= 101 && right >= 200 && right <= 201 &&
              bottom >= 200 && bottom <= 201 && corner >= 200 && corner <= 201,
              QString("%1 x %2; inside %3, right edge %4, bottom edge %5, corner %6 (100 and 200 in 8 bits)").arg(dw).arg(dh).arg(inner).arg(right).arg(bottom).arg(corner));
    }
    {
        // White stays white and never wraps around; a 12-bit picture works the same.
        std::vector<uint16_t> w10(64 * 16, 1023), w12(64 * 16, 4095);
        int dw, dh;
        bool ok = true;
        for (int k = 1; k <= 4; ++k) {
            for (uint8_t v : shrink16(w10, 64, 16, 10, k, &dw, &dh)) ok = ok && v == 255;
            for (uint8_t v : shrink16(w12, 64, 16, 12, k, &dw, &dh)) ok = ok && v == 255;
        }
        check("full white stays 255 (10- and 12-bit, factors 1 to 4)", ok, "no value wraps around");
    }
    {
        // A smooth ramp keeps its in-between levels on average (the dither): 4 output steps span 16 input steps.
        const int sw = 256, sh = 64;
        std::vector<uint16_t> ramp(size_t(sw) * sh);
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < sw; ++x) ramp[size_t(y) * sw + x] = uint16_t(400 + x / 16);   // 400..415: four 8-bit levels
        int dw, dh;
        const std::vector<uint8_t> out = shrink16(ramp, sw, sh, 10, 1, &dw, &dh);
        double worst = 0;
        for (int band = 0; band < 16; ++band) {
            double sum = 0;
            for (int y = 0; y < sh; ++y)
                for (int x = band * 16; x < band * 16 + 16; ++x) sum += out[size_t(y) * dw + x];
            worst = std::max(worst, std::abs(sum / (16.0 * sh) - (400 + band) / 4.0));
        }
        check("a smooth 10-bit ramp keeps its in-between levels on average (dither, no banding)", worst < 0.07,
              QString("largest error of a level's average: %1 of one 8-bit step").arg(worst, 0, 'f', 3));
    }

    // ---- which pictures nothing is built from
    using G = FrameGovernor;
    auto h264 = [](uint8_t header, bool lengths) {
        std::vector<uint8_t> v;
        auto unit = [&](std::initializer_list<uint8_t> bytes) {
            if (lengths) { v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(uint8_t(bytes.size())); }
            else { v.push_back(0); v.push_back(0); v.push_back(1); }
            v.insert(v.end(), bytes);
        };
        unit({0x09, 0xf0});              // access unit delimiter
        unit({0x06, 0x05, 0x01, 0x80});  // SEI
        unit({header, 0x88, 0x84, 0x00, 0x33});
        return v;
    };
    {
        const auto ref = h264(0x41, true), unref = h264(0x01, true), idr = h264(0x65, true), odd = h264(0x05, true);
        const auto unrefSc = h264(0x01, false), refSc = h264(0x61, false);
        check("H.264: a slice with reference level 0 is one nothing is built from",
              G::unreferenced(G::Codec::H264, unref.data(), unref.size(), 4) && G::unreferenced(G::Codec::H264, unrefSc.data(), unrefSc.size(), 0),
              "with lengths and with start codes");
        check("H.264: reference pictures and key pictures are not",
              !G::unreferenced(G::Codec::H264, ref.data(), ref.size(), 4) && !G::unreferenced(G::Codec::H264, idr.data(), idr.size(), 4) &&
              !G::unreferenced(G::Codec::H264, odd.data(), odd.size(), 4) && !G::unreferenced(G::Codec::H264, refSc.data(), refSc.size(), 0),
              "reference level 2 and 3, a key picture, a key picture wrongly marked 0");
    }
    auto h265 = [](int type, int layer, bool lengths) {
        std::vector<uint8_t> v;
        auto unit = [&](std::initializer_list<uint8_t> bytes) {
            if (lengths) { v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(uint8_t(bytes.size())); }
            else { v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(1); }
            v.insert(v.end(), bytes);
        };
        unit({uint8_t(35 << 1), 0x01, 0x50});                 // access unit delimiter
        unit({uint8_t(39 << 1), 0x01, 0x01, 0x05, 0x80});     // SEI
        unit({uint8_t(type << 1), uint8_t(layer + 1), 0xaf, 0x08, 0x40});
        return v;
    };
    {
        int layer = -9;
        const auto n0 = h265(0, 0, true), r1 = h265(1, 0, true), idr = h265(19, 0, true), rasl = h265(8, 2, false), cra = h265(21, 0, false);
        const bool a = G::unreferenced(G::Codec::H265, n0.data(), n0.size(), 4, &layer);
        check("H.265: TRAIL_N is one nothing is built from, TRAIL_R and key pictures are not",
              a && layer == 0 && !G::unreferenced(G::Codec::H265, r1.data(), r1.size(), 4) && !G::unreferenced(G::Codec::H265, idr.data(), idr.size(), 4) &&
              !G::unreferenced(G::Codec::H265, cra.data(), cra.size(), 0), QString("temporal layer read: %1").arg(layer));
        const bool b = G::unreferenced(G::Codec::H265, rasl.data(), rasl.size(), 0, &layer);
        check("H.265: with start codes, and the temporal layer is read", b && layer == 2, QString("RASL_N in layer %1").arg(layer));
    }
    {
        const uint8_t junk[] = {0, 0, 0, 200, 0x41, 0x9a};   // a length that runs past the end
        const uint8_t none[] = {1, 2, 3, 4, 5, 6, 7, 8};
        check("cut-off or unreadable data counts as a picture to keep",
              !G::unreferenced(G::Codec::H264, junk, 4, 4) && !G::unreferenced(G::Codec::H264, none, sizeof none, 0) &&
              !G::unreferenced(G::Codec::H265, none, sizeof none, 4) && !G::unreferenced(G::Codec::H264, nullptr, 0, 4) &&
              !G::unreferenced(G::Codec::None, junk, sizeof junk, 4), "nothing is left out on a guess");
    }
    std::printf("\n%s\n", g_fail ? "shrink / governor checks FAILED" : "All shrink / governor checks passed");
    return g_fail ? 1 : 0;
}
