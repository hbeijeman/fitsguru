#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fg {

// A single 2-D image plane, converted to float (BZERO/BSCALE applied).
struct FitsImage {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;  // row-major, FITS row order (row 0 = bottom)
    int bitpix = 0;             // original BITPIX of the HDU
    std::string bayerPattern;   // BAYERPAT keyword, empty if absent
};

// Loads the first HDU containing a 2-D (or larger) image; for NAXIS > 2
// only the first plane is read. Throws std::runtime_error on failure.
FitsImage loadFits(const std::string& path);

}  // namespace fg
