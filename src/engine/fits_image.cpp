#include "fits_image.h"

#include <fitsio.h>

#include <memory>
#include <mutex>
#include <stdexcept>

namespace fg {

namespace {

[[noreturn]] void throwFits(int status, const std::string& what) {
    char msg[FLEN_ERRMSG] = {};
    fits_get_errstatus(status, msg);
    throw std::runtime_error(what + ": " + msg);
}

// CFITSIO built without --enable-reentrant must not be entered concurrently.
std::unique_lock<std::mutex> fitsLock() {
    static std::mutex mutex;
    static const bool reentrant = fits_is_reentrant();
    return reentrant ? std::unique_lock<std::mutex>() : std::unique_lock<std::mutex>(mutex);
}

}  // namespace

FitsFile::FitsFile(const std::string& path) : path_(path) {
    const auto lock = fitsLock();
    int status = 0;
    fitsfile* fptr = nullptr;
    // fits_open_image moves to the first HDU that holds an image.
    if (fits_open_image(&fptr, path.c_str(), READONLY, &status))
        throwFits(status, "cannot open '" + path + "'");
    handle_ = fptr;

    FitsImage& h = header_;
    int naxis = 0;
    long naxes[3] = {0, 0, 0};
    fits_get_img_type(fptr, &h.bitpix, &status);
    fits_get_img_dim(fptr, &naxis, &status);
    fits_get_img_size(fptr, 3, naxes, &status);
    if (status || naxis < 2) {
        int s2 = 0;
        fits_close_file(fptr, &s2);
        handle_ = nullptr;
        if (status) throwFits(status, "cannot read image geometry of '" + path + "'");
        throw std::runtime_error("'" + path + "': HDU is not a 2-D image");
    }
    h.width = static_cast<int>(naxes[0]);
    h.height = static_cast<int>(naxes[1]);
    h.planes = naxis >= 3 ? static_cast<int>(naxes[2]) : 1;

    char bayer[FLEN_VALUE] = {};
    int kstatus = 0;
    if (fits_read_key(fptr, TSTRING, "BAYERPAT", bayer, nullptr, &kstatus) == 0) {
        h.bayerPattern = bayer;
        h.bayerPattern.erase(h.bayerPattern.find_last_not_of(' ') + 1);
    }
    kstatus = 0;
    fits_read_key(fptr, TINT, "XBAYROFF", &h.bayerXOffset, nullptr, &kstatus);
    kstatus = 0;
    fits_read_key(fptr, TINT, "YBAYROFF", &h.bayerYOffset, nullptr, &kstatus);
}

FitsFile::~FitsFile() {
    const auto lock = fitsLock();
    int status = 0;
    if (handle_) fits_close_file(static_cast<fitsfile*>(handle_), &status);
}

void FitsFile::readRow(int y, float* dst) {
    const auto lock = fitsLock();
    long fpixel[3] = {1, y + 1L, 1};
    float nullval = 0.0f;  // 0 => no null substitution; NaNs pass through
    int anynull = 0, status = 0;
    if (fits_read_pix(static_cast<fitsfile*>(handle_), TFLOAT, fpixel, header_.width, &nullval, dst, &anynull, &status))
        throwFits(status, "cannot read pixel data of '" + path_ + "'");
}

FitsImage FitsFile::readAll() {
    const auto lock = fitsLock();
    FitsImage img = header_;
    const long long n = static_cast<long long>(img.width) * img.height;
    img.pixels.resize(static_cast<size_t>(n));
    long fpixel[3] = {1, 1, 1};
    float nullval = 0.0f;
    int anynull = 0, status = 0;
    if (fits_read_pix(static_cast<fitsfile*>(handle_), TFLOAT, fpixel, n, &nullval, img.pixels.data(), &anynull, &status))
        throwFits(status, "cannot read pixel data of '" + path_ + "'");
    return img;
}

FitsImage loadFits(const std::string& path) {
    return FitsFile(path).readAll();
}

}  // namespace fg
