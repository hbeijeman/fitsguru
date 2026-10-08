#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "fits_image.h"

namespace fg {

// Display-ready pixel buffer, independent of any GUI toolkit.
struct DisplayBuffer {
    int width = 0;
    int height = 0;
    int channels = 1;  // 1 = Gray8, 3 = RGB888 (interleaved)
    int stride = 0;    // bytes per row
    std::vector<uint8_t> data;  // row 0 = top of the display
};

struct StretchParams {
    float shadowsClipMads = -2.8f;  // shadows clip, in normalized MADs from median
    float targetBackground = 0.25f; // where the median lands after stretch
};

// Colour (0 = R, 1 = G, 2 = B) of each CFA site, indexed by
// ((y + yoff) & 1) * 2 + ((x + xoff) & 1) in stored (FITS) pixel coordinates.
struct BayerPattern {
    std::array<int, 4> colors;
    int xOffset = 0;
    int yOffset = 0;
};

// The image's CFA layout, if it is a single plane with a valid BAYERPAT
// (RGGB, BGGR, GRBG or GBRG).
std::optional<BayerPattern> bayerPattern(const FitsImage& img);

// Maps raw pixel values to [0,255] display values (shadows clip + midtones
// transfer function).
struct Curve {
    float offset = 0, scale = 1, m = 0.5f;

    uint8_t operator()(float v) const {
        float x = std::isfinite(v) ? (v - offset) * scale : 0.0f;
        if (x <= 0.0f) return 0;
        if (x >= 1.0f) return 255;
        x = ((m - 1.0f) * x) / ((2.0f * m - 1.0f) * x - m);
        return static_cast<uint8_t>(x * 255.0f + 0.5f);
    }
};

// One curve for mono, or one per colour channel (unlinked) for Bayer data.
using StretchCurves = std::array<Curve, 3>;

// Auto-stretch statistics (median/MAD). With `bayer`, per CFA colour.
// Computing these on a decimated preview and reusing them for the full image
// keeps the two renderings visually identical.
StretchCurves computeStretch(const FitsImage& img, const std::optional<BayerPattern>& bayer,
                             const StretchParams& p = {});

// Gray8 rendering, flipped so FITS row 0 ends up at the bottom of the display.
DisplayBuffer applyStretch(const FitsImage& img, const StretchCurves& curves);

// RGB888 rendering with a nearest-neighbour demosaic: each 2x2 CFA cell
// becomes one colour (G averaged) written to all four of its pixels.
DisplayBuffer applyStretchBayer(const FitsImage& img, const BayerPattern& bayer,
                                const StretchCurves& curves);

inline DisplayBuffer autoStretch(const FitsImage& img, const StretchParams& p = {}) {
    return applyStretch(img, computeStretch(img, std::nullopt, p));
}

inline DisplayBuffer autoStretchBayer(const FitsImage& img, const BayerPattern& bayer,
                                      const StretchParams& p = {}) {
    return applyStretchBayer(img, bayer, computeStretch(img, bayer, p));
}

}  // namespace fg
