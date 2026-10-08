#pragma once

#include <array>
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

// Automatic screen stretch (median/MAD shadows clip + midtones transfer
// function), flipped so FITS row 0 ends up at the bottom of the display.
DisplayBuffer autoStretch(const FitsImage& img, const StretchParams& p = {});

// Same stretch per colour channel (unlinked), combined with a nearest-
// neighbour demosaic: each 2x2 CFA cell becomes one RGB colour (G averaged)
// written to all four of its pixels. Output is RGB888 at full resolution.
DisplayBuffer autoStretchBayer(const FitsImage& img, const BayerPattern& bayer,
                               const StretchParams& p = {});

}  // namespace fg
