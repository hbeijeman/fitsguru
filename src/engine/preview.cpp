#include "preview.h"

#include <algorithm>
#include <vector>

namespace fg {

int previewStep(int width, int height, int maxSide) {
    const int side = std::max(width, height);
    return side <= maxSide ? 1 : (side + maxSide - 1) / maxSide;
}

Preview loadPreview(const std::string& path, int maxSide) {
    FitsFile file(path);
    Preview p;
    const FitsImage& h = file.header();
    p.fullWidth = h.width;
    p.fullHeight = h.height;
    p.step = previewStep(h.width, h.height, maxSide);
    p.bayer = bayerPattern(h);
    const int step = p.step;

    if (step == 1) {
        p.image = file.readAll();
    } else {
        FitsImage& out = p.image;
        out = h;  // header only; pixels empty
        std::vector<float> row(static_cast<size_t>(h.width));
        if (p.bayer) {
            // Whole cells at (2*k*step, 2*k*step): CFA phase is unchanged.
            const int cellsX = (h.width / 2 + step - 1) / step;
            const int cellsY = (h.height / 2 + step - 1) / step;
            out.width = 2 * cellsX;
            out.height = 2 * cellsY;
            out.pixels.resize(static_cast<size_t>(out.width) * out.height);
            for (int y = 0; y < out.height; ++y) {
                file.readRow((y / 2) * 2 * step + (y & 1), row.data());
                float* dst = out.pixels.data() + static_cast<size_t>(y) * out.width;
                for (int cx = 0; cx < cellsX; ++cx) {
                    dst[2 * cx] = row[2 * cx * step];
                    dst[2 * cx + 1] = row[2 * cx * step + 1];
                }
            }
        } else {
            out.width = (h.width + step - 1) / step;
            out.height = (h.height + step - 1) / step;
            out.pixels.resize(static_cast<size_t>(out.width) * out.height);
            for (int y = 0; y < out.height; ++y) {
                file.readRow(y * step, row.data());
                float* dst = out.pixels.data() + static_cast<size_t>(y) * out.width;
                for (int x = 0; x < out.width; ++x) dst[x] = row[x * step];
            }
        }
    }

    p.monoCurves = computeStretch(p.image, std::nullopt);
    if (p.bayer) p.bayerCurves = computeStretch(p.image, p.bayer);
    return p;
}

}  // namespace fg
