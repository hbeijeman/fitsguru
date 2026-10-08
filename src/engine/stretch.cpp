#include "stretch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace fg {

namespace {

// Midtones transfer function: maps m -> 0.5, 0 -> 0, 1 -> 1.
inline float mtf(float m, float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return ((m - 1.0f) * x) / ((2.0f * m - 1.0f) * x - m);
}

// Maps raw pixel values to [0,255] display values.
struct Curve {
    float offset = 0, scale = 1, m = 0.5f;

    uint8_t operator()(float v) const {
        const float t = std::isfinite(v) ? mtf(m, (v - offset) * scale) : 0.0f;
        return static_cast<uint8_t>(t * 255.0f + 0.5f);
    }
};

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

// Runs fn(y0, y1) over [0, rows) split across all cores.
template <typename Fn>
void parallelRows(int rows, Fn fn) {
    const int nThreads = std::max(1u, std::thread::hardware_concurrency());
    const int chunk = (rows + nThreads - 1) / nThreads;
    std::vector<std::thread> pool;
    for (int i = 0; i < nThreads; ++i) {
        const int y0 = i * chunk, y1 = std::min(rows, y0 + chunk);
        if (y0 < y1) pool.emplace_back(fn, y0, y1);
    }
    for (auto& t : pool) t.join();
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

DisplayBuffer autoStretch(const FitsImage& img, const StretchParams& p) {
    DisplayBuffer out = makeBuffer(img.width, img.height, 1);
    if (img.pixels.empty()) return out;

    Range r;
    for (float v : img.pixels)  // full pass for exact min/max
        if (std::isfinite(v)) r.add(v);
    const size_t step = std::max<size_t>(1, img.pixels.size() / kTargetSamples);
    std::vector<float> s;
    s.reserve(img.pixels.size() / step + 1);
    for (size_t i = 0; i < img.pixels.size(); i += step)
        if (std::isfinite(img.pixels[i])) s.push_back(img.pixels[i]);
    const Curve curve = makeCurve(s, r, p);

    parallelRows(img.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* src = img.pixels.data() + static_cast<size_t>(y) * img.width;
            uint8_t* dst = out.data.data() + static_cast<size_t>(img.height - 1 - y) * out.stride;
            for (int x = 0; x < img.width; ++x) dst[x] = curve(src[x]);
        }
    });
    return out;
}

DisplayBuffer autoStretchBayer(const FitsImage& img, const BayerPattern& bayer,
                               const StretchParams& p) {
    const int w = img.width, h = img.height;
    DisplayBuffer out = makeBuffer(w, h, 3);
    if (img.pixels.empty()) return out;
    auto colorAt = [&](int x, int y) {
        return bayer.colors[((y + bayer.yOffset) & 1) * 2 + ((x + bayer.xOffset) & 1)];
    };

    // Per-channel statistics: exact min/max, sampled median/MAD.
    std::array<Range, 3> ranges;
    std::array<std::vector<float>, 3> samples;
    const size_t step = std::max<size_t>(1, img.pixels.size() / kTargetSamples);
    size_t i = 0;
    for (int y = 0; y < h; ++y) {
        const int c0 = colorAt(0, y), c1 = colorAt(1, y);
        const float* row = img.pixels.data() + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x, ++i) {
            const float v = row[x];
            if (!std::isfinite(v)) continue;
            const int c = (x & 1) ? c1 : c0;
            ranges[c].add(v);
            // Odd step keeps sampling from locking onto one CFA phase.
            if (i % (step | 1) == 0) samples[c].push_back(v);
        }
    }
    std::array<Curve, 3> curves;
    for (int c = 0; c < 3; ++c) curves[c] = makeCurve(samples[c], ranges[c], p);

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
