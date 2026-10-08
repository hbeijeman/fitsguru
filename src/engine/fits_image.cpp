#include "fits_image.h"

#include <fitsio.h>

#include <stdexcept>

namespace fg {

namespace {

[[noreturn]] void throwFits(int status, const std::string& what) {
    char msg[FLEN_ERRMSG] = {};
    fits_get_errstatus(status, msg);
    throw std::runtime_error(what + ": " + msg);
}

}  // namespace

FitsImage loadFits(const std::string& path) {
    fitsfile* fptr = nullptr;
    int status = 0;
    // fits_open_image moves to the first HDU that holds an image.
    if (fits_open_image(&fptr, path.c_str(), READONLY, &status))
        throwFits(status, "cannot open '" + path + "'");

    FitsImage img;
    int naxis = 0;
    long naxes[3] = {0, 0, 0};
    fits_get_img_type(fptr, &img.bitpix, &status);
    fits_get_img_dim(fptr, &naxis, &status);
    fits_get_img_size(fptr, 3, naxes, &status);
    if (status) {
        fits_close_file(fptr, &status);
        throwFits(status, "cannot read image geometry");
    }
    if (naxis < 2) {
        fits_close_file(fptr, &status);
        throw std::runtime_error("HDU is not a 2-D image");
    }
    img.width = static_cast<int>(naxes[0]);
    img.height = static_cast<int>(naxes[1]);
    img.planes = naxis >= 3 ? static_cast<int>(naxes[2]) : 1;

    char bayer[FLEN_VALUE] = {};
    int kstatus = 0;
    if (fits_read_key(fptr, TSTRING, "BAYERPAT", bayer, nullptr, &kstatus) == 0) {
        img.bayerPattern = bayer;
        img.bayerPattern.erase(img.bayerPattern.find_last_not_of(' ') + 1);
    }
    kstatus = 0;
    fits_read_key(fptr, TINT, "XBAYROFF", &img.bayerXOffset, nullptr, &kstatus);
    kstatus = 0;
    fits_read_key(fptr, TINT, "YBAYROFF", &img.bayerYOffset, nullptr, &kstatus);

    const long long n = static_cast<long long>(img.width) * img.height;
    img.pixels.resize(static_cast<size_t>(n));
    long fpixel[3] = {1, 1, 1};
    float nullval = 0.0f;  // 0 => no null substitution; NaNs pass through
    int anynull = 0;
    fits_read_pix(fptr, TFLOAT, fpixel, n, &nullval, img.pixels.data(), &anynull, &status);
    if (status) {
        int s2 = 0;
        fits_close_file(fptr, &s2);
        throwFits(status, "cannot read pixel data");
    }
    fits_close_file(fptr, &status);
    return img;
}

}  // namespace fg
