#include "stretch.h"

#include <algorithm>
#include <limits>

#include "parallel.h"

namespace fg {

namespace {

// Midtones transfer function: maps m -> 0.5, 0 -> 0, 1 -> 1.
inline float mtf(float m, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return ((m - 1.0f) * x) / ((2.0f * m - 1.0f) * x - m);
}

struct Range {
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    void add(float v) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
};

// Builds the auto-stretch curve from a sample of finite pixels (reordered in
// place) and the channel's exact min/max.
Curve makeCurve(std::vector<float>& s, Range r, const StretchParams& p) {
    if (s.empty() || r.hi < r.lo) return {};
    auto mid = s.begin() + s.size() / 2;
    std::nth_element(s.begin(), mid, s.end());
    const float median = *mid;
    for (float& v : s) v = std::fabs(v - median);
    std::nth_element(s.begin(), mid, s.end());
    const float mad = *mid;

    const float range = r.hi > r.lo ? r.hi - r.lo : 1.0f;
    // Work in [0,1]-normalized units.
    const float med = (median - r.lo) / range;
    const float madn = 1.4826f * mad / range;
    const float c0 = std::clamp(med + p.shadowsClipMads * madn, 0.0f, 1.0f);
    const float span = std::max(1.0f - c0, 1e-6f);
    Curve c;
    c.m = mtf(p.targetBackground, std::max(med - c0, 1e-6f) / span);
    c.scale = 1.0f / (range * span);
    c.offset = r.lo + c0 * range;
    return c;
}

DisplayBuffer makeBuffer(int width, int height, int channels) {
    DisplayBuffer out;
    out.width = width;
    out.height = height;
    out.channels = channels;
    out.stride = (width * channels + 3) & ~3;  // 32-bit aligned rows (QImage-friendly)
    out.data.assign(static_cast<size_t>(out.stride) * height, 0);
    return out;
}

constexpr size_t kTargetSamples = 1'000'000;

}  // namespace

std::optional<BayerPattern> bayerPattern(const FitsImage& img) {
    if (img.planes != 1 || img.bayerPattern.size() != 4) return std::nullopt;
    BayerPattern b;
    for (int i = 0; i < 4; ++i) {
        switch (img.bayerPattern[i]) {
            case 'R': case 'r': b.colors[i] = 0; break;
            case 'G': case 'g': b.colors[i] = 1; break;
            case 'B': case 'b': b.colors[i] = 2; break;
            default: return std::nullopt;
        }
    }
    // Must be a real Bayer layout: one R, one B, two diagonal G.
    if (b.colors[0] != b.colors[3] && b.colors[1] != b.colors[2]) return std::nullopt;
    if (std::count(b.colors.begin(), b.colors.end(), 1) != 2 ||
        std::count(b.colors.begin(), b.colors.end(), 0) != 1)
        return std::nullopt;
    b.xOffset = img.bayerXOffset & 1;
    b.yOffset = img.bayerYOffset & 1;
    return b;
}

StretchCurves computeStretch(const FitsImage& img, const std::optional<BayerPattern>& bayer,
                             const StretchParams& p) {
    StretchCurves curves;
    if (img.pixels.empty()) return curves;
    const int w = img.width, h = img.height;
    // Channel of each CFA site; all zero for mono.
    std::array<int, 4> site = {0, 0, 0, 0};
    if (bayer) site = bayer->colors;
    const int xo = bayer ? bayer->xOffset : 0, yo = bayer ? bayer->yOffset : 0;

    // Exact min/max, sampled median/MAD.
    std::array<Range, 3> ranges;
    std::array<std::vector<float>, 3> samples;
    // Odd step keeps sampling from locking onto one CFA phase.
    const size_t step = std::max<size_t>(1, img.pixels.size() / kTargetSamples) | 1;
    size_t i = 0;
    for (int y = 0; y < h; ++y) {
        const int c0 = site[((y + yo) & 1) * 2 + (xo & 1)];
        const int c1 = site[((y + yo) & 1) * 2 + ((1 + xo) & 1)];
        const float* row = img.pixels.data() + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x, ++i) {
            const float v = row[x];
            if (!std::isfinite(v)) continue;
            const int c = (x & 1) ? c1 : c0;
            ranges[c].add(v);
            if (i % step == 0) samples[c].push_back(v);
        }
    }
    for (int c = 0; c < 3; ++c) curves[c] = makeCurve(samples[c], ranges[c], p);
    if (!bayer) curves[1] = curves[2] = curves[0];
    return curves;
}

DisplayBuffer applyStretch(const FitsImage& img, const StretchCurves& curves) {
    DisplayBuffer out = makeBuffer(img.width, img.height, 1);
    if (img.pixels.empty()) return out;
    const Curve curve = curves[0];
    parallelRows(img.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* src = img.pixels.data() + static_cast<size_t>(y) * img.width;
            uint8_t* dst = out.data.data() + static_cast<size_t>(img.height - 1 - y) * out.stride;
            for (int x = 0; x < img.width; ++x) dst[x] = curve(src[x]);
        }
    });
    return out;
}

DisplayBuffer applyStretchBayer(const FitsImage& img, const BayerPattern& bayer,
                                const StretchCurves& curves) {
    const int w = img.width, h = img.height;
    DisplayBuffer out = makeBuffer(w, h, 3);
    if (img.pixels.empty()) return out;
    auto colorAt = [&](int x, int y) {
        return bayer.colors[((y + bayer.yOffset) & 1) * 2 + ((x + bayer.xOffset) & 1)];
    };

    // Nearest-neighbour demosaic, one 2x2 cell at a time (edges clamp).
    const int cellsY = (h + 1) / 2, cellsX = (w + 1) / 2;
    parallelRows(cellsY, [&](int cy0, int cy1) {
        for (int cy = cy0; cy < cy1; ++cy) {
            const int ya = 2 * cy, yb = std::min(ya + 1, h - 1);
            const float* ra = img.pixels.data() + static_cast<size_t>(ya) * w;
            const float* rb = img.pixels.data() + static_cast<size_t>(yb) * w;
            uint8_t* da = out.data.data() + static_cast<size_t>(h - 1 - ya) * out.stride;
            uint8_t* db = out.data.data() + static_cast<size_t>(h - 1 - yb) * out.stride;
            const int caa = colorAt(0, ya), cab = colorAt(1, ya);
            const int cba = colorAt(0, yb), cbb = colorAt(1, yb);
            for (int cx = 0; cx < cellsX; ++cx) {
                const int xa = 2 * cx, xb = std::min(xa + 1, w - 1);
                float sum[3] = {0, 0, 0};
                int n[3] = {0, 0, 0};
                sum[caa] += ra[xa]; ++n[caa];
                sum[cab] += ra[xb]; ++n[cab];
                sum[cba] += rb[xa]; ++n[cba];
                sum[cbb] += rb[xb]; ++n[cbb];
                uint8_t rgb[3];
                for (int c = 0; c < 3; ++c) rgb[c] = n[c] ? curves[c](sum[c] / n[c]) : 0;
                for (uint8_t* d : {da + 3 * xa, da + 3 * xb, db + 3 * xa, db + 3 * xb}) {
                    d[0] = rgb[0];
                    d[1] = rgb[1];
                    d[2] = rgb[2];
                }
            }
        }
    });
    return out;
}

}  // namespace fg
