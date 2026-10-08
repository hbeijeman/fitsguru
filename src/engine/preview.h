#pragma once

#include <optional>
#include <string>

#include "fits_image.h"
#include "stretch.h"

namespace fg {

// A display-sized stand-in for a FITS image, cheap enough to keep in memory
// for a whole folder.
struct Preview {
    FitsImage image;     // decimated mosaic/mono data (the image itself if small)
    int step = 1;        // source pixels per preview pixel along each axis
    int fullWidth = 0;   // size of the source image
    int fullHeight = 0;
    std::optional<BayerPattern> bayer;
    StretchCurves monoCurves;                  // auto stretch, all pixels as one channel
    std::optional<StretchCurves> bayerCurves;  // per CFA colour, if `bayer`
};

// Integer decimation step that brings the larger side to <= maxSide
// (1 = image is small enough, no preview needed).
int previewStep(int width, int height, int maxSide = 4096);

// Reads a preview straight from disk by skipping rows/columns: only the rows
// that are kept are read. Keeps every step-th pixel, or, if the image has a
// usable BAYERPAT, every step-th whole 2x2 CFA cell taken from even
// coordinates, so the preview is still a valid mosaic with the same pattern
// and can be debayered. Also precomputes the stretch curves. Thread-safe.
Preview loadPreview(const std::string& path, int maxSide = 4096);

}  // namespace fg
