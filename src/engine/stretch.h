#pragma once

#include <cstdint>
#include <vector>

#include "fits_image.h"

namespace fg {

// Display-ready pixel buffer, independent of any GUI toolkit.
struct DisplayBuffer {
    int width = 0;
    int height = 0;
    int stride = 0;  // bytes per row
    std::vector<uint8_t> data;  // Gray8, row 0 = top of the display
};

struct StretchParams {
    float shadowsClipMads = -2.8f;  // shadows clip, in normalized MADs from median
    float targetBackground = 0.25f; // where the median lands after stretch
};

// Automatic screen stretch (median/MAD shadows clip + midtones transfer
// function), flipped so FITS row 0 ends up at the bottom of the display.
DisplayBuffer autoStretch(const FitsImage& img, const StretchParams& p = {});

}  // namespace fg
