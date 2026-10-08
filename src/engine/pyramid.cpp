#include "pyramid.h"

#include <algorithm>

namespace fg {

namespace {

DisplayBuffer halve(const DisplayBuffer& in) {
    const int c = in.channels;
    DisplayBuffer out;
    out.width = (in.width + 1) / 2;
    out.height = (in.height + 1) / 2;
    out.channels = c;
    out.stride = (out.width * c + 3) & ~3;
    out.data.assign(static_cast<size_t>(out.stride) * out.height, 0);
    for (int y = 0; y < out.height; ++y) {
        const int y0 = 2 * y, y1 = std::min(y0 + 1, in.height - 1);
        const uint8_t* r0 = in.data.data() + static_cast<size_t>(y0) * in.stride;
        const uint8_t* r1 = in.data.data() + static_cast<size_t>(y1) * in.stride;
        uint8_t* dst = out.data.data() + static_cast<size_t>(y) * out.stride;
        for (int x = 0; x < out.width; ++x) {
            const int x0 = 2 * x * c, x1 = std::min(2 * x + 1, in.width - 1) * c;
            for (int k = 0; k < c; ++k)
                dst[x * c + k] = static_cast<uint8_t>(
                    (r0[x0 + k] + r0[x1 + k] + r1[x0 + k] + r1[x1 + k] + 2) / 4);
        }
    }
    return out;
}

}  // namespace

std::vector<DisplayBuffer> buildPyramid(DisplayBuffer base, int minSize) {
    std::vector<DisplayBuffer> levels;
    levels.push_back(std::move(base));
    while (std::max(levels.back().width, levels.back().height) > minSize)
        levels.push_back(halve(levels.back()));
    return levels;
}

}  // namespace fg
