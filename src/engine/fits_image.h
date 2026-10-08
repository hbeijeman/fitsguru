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
    int planes = 1;             // NAXIS3 (1 for a plain 2-D image)
    int bitpix = 0;             // original BITPIX of the HDU
    std::string bayerPattern;   // BAYERPAT keyword, trimmed; empty if absent
    int bayerXOffset = 0;       // XBAYROFF
    int bayerYOffset = 0;       // YBAYROFF
};

// An open FITS file positioned on its first image HDU. Reads the header on
// construction; pixels are read on demand, either whole or row by row (only
// the first plane). Throws std::runtime_error on failure. Safe to use from
// several threads as long as each thread has its own FitsFile.
class FitsFile {
public:
    explicit FitsFile(const std::string& path);
    ~FitsFile();
    FitsFile(const FitsFile&) = delete;
    FitsFile& operator=(const FitsFile&) = delete;

    // Geometry and keywords; `pixels` is empty.
    const FitsImage& header() const { return header_; }

    // Reads stored row y (0-based) into dst[0 .. width).
    void readRow(int y, float* dst);

    // Reads the whole first plane.
    FitsImage readAll();

private:
    std::string path_;
    void* handle_ = nullptr;  // fitsfile*; keeps fitsio.h out of this header
    FitsImage header_;
};

// Loads the first HDU containing a 2-D (or larger) image; for NAXIS > 2
// only the first plane is read. Throws std::runtime_error on failure.
FitsImage loadFits(const std::string& path);

}  // namespace fg
