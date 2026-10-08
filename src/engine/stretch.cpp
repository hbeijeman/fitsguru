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

struct Stats {
    float min, max, median, mad;
};

// Statistics from a strided subsample of finite pixels (~1M samples).
Stats sampleStats(const std::vector<float>& px) {
    const size_t step = std::max<size_t>(1, px.size() / 1'000'000);
    std::vector<float> s;
    s.reserve(px.size() / step + 1);
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (float v : px) {  // full pass for exact min/max
        if (!std::isfinite(v)) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    for (size_t i = 0; i < px.size(); i += step)
        if (std::isfinite(px[i])) s.push_back(px[i]);
    if (s.empty()) return {0, 1, 0, 0};

    auto mid = s.begin() + s.size() / 2;
    std::nth_element(s.begin(), mid, s.end());
    const float median = *mid;
    for (float& v : s) v = std::fabs(v - median);
    std::nth_element(s.begin(), mid, s.end());
    return {lo, hi, median, *mid};
}

}  // namespace

DisplayBuffer autoStretch(const FitsImage& img, const StretchParams& p) {
    DisplayBuffer out;
    out.width = img.width;
    out.height = img.height;
    out.stride = (img.width + 3) & ~3;  // 32-bit aligned rows (QImage-friendly)
    out.data.assign(static_cast<size_t>(out.stride) * img.height, 0);
    if (img.pixels.empty()) return out;

    const Stats st = sampleStats(img.pixels);
    const float range = st.max > st.min ? st.max - st.min : 1.0f;
    // Work in [0,1]-normalized units.
    const float med = (st.median - st.min) / range;
    const float madn = 1.4826f * st.mad / range;
    const float c0 = std::clamp(med + p.shadowsClipMads * madn, 0.0f, 1.0f);
    const float m = mtf(p.targetBackground, std::max(med - c0, 1e-6f) / std::max(1.0f - c0, 1e-6f));
    const float scale = 1.0f / (range * std::max(1.0f - c0, 1e-6f));
    const float offset = st.min + c0 * range;

    auto worker = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* src = img.pixels.data() + static_cast<size_t>(y) * img.width;
            uint8_t* dst = out.data.data() + static_cast<size_t>(img.height - 1 - y) * out.stride;
            for (int x = 0; x < img.width; ++x) {
                const float v = src[x];
                const float t = std::isfinite(v) ? mtf(m, (v - offset) * scale) : 0.0f;
                dst[x] = static_cast<uint8_t>(t * 255.0f + 0.5f);
            }
        }
    };

    const int nThreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    const int chunk = (img.height + nThreads - 1) / nThreads;
    for (int i = 0; i < nThreads; ++i) {
        const int y0 = i * chunk, y1 = std::min(img.height, y0 + chunk);
        if (y0 < y1) pool.emplace_back(worker, y0, y1);
    }
    for (auto& t : pool) t.join();
    return out;
}

}  // namespace fg
