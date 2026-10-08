#include "image_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kMaxScale = 32.0;    // 1 image pixel -> 32 screen pixels
constexpr double kWheelStep = 1.25;   // zoom factor per wheel notch
constexpr int kInsetMaxSide = 200;
constexpr int kInsetMargin = 10;

}  // namespace

ImageView::ImageView(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::OpenHandCursor);
}

void ImageView::setFrame(std::vector<fg::DisplayBuffer> pyramid) {
    const QSizeF oldSize = imageSize();
    pyramid_ = std::move(pyramid);
    levels_.clear();
    for (const auto& b : pyramid_)
        levels_.emplace_back(b.data.data(), b.width, b.height, b.stride,
                             b.channels == 3 ? QImage::Format_RGB888 : QImage::Format_Grayscale8);
    // Same geometry (e.g. re-rendered with other options): keep zoom and pan.
    if (!fitMode_ && imageSize() == oldSize) {
        update();
    } else {
        fitToView();
    }
}

QSizeF ImageView::imageSize() const {
    return levels_.empty() ? QSizeF() : QSizeF(levels_.front().size());
}

double ImageView::fitScale() const {
    const QSizeF img = imageSize();
    if (img.isEmpty()) return 1.0;
    return std::min(width() / img.width(), height() / img.height());
}

void ImageView::fitToView() {
    fitMode_ = true;
    scale_ = fitScale();
    center_ = QPointF(imageSize().width() / 2, imageSize().height() / 2);
    update();
}

// Keep the image covering the view where it can; centre it along any axis
// where it is smaller than the view.
void ImageView::clampCenter() {
    const QSizeF img = imageSize();
    const double halfW = width() / (2 * scale_), halfH = height() / (2 * scale_);
    center_.setX(img.width() <= 2 * halfW ? img.width() / 2
                                          : std::clamp(center_.x(), halfW, img.width() - halfW));
    center_.setY(img.height() <= 2 * halfH ? img.height() / 2
                                           : std::clamp(center_.y(), halfH, img.height() - halfH));
}

QPointF ImageView::widgetToImage(QPointF p) const {
    return center_ + (p - QPointF(width() / 2.0, height() / 2.0)) / scale_;
}

// Coarsest pyramid level that still has at least one texel per device pixel.
const QImage& ImageView::levelFor(double devicePixelsPerImagePixel, int* level) const {
    int l = 0;
    while (l + 1 < static_cast<int>(levels_.size()) && devicePixelsPerImagePixel * (1 << (l + 1)) <= 1.0)
        ++l;
    *level = l;
    return levels_[l];
}

void ImageView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (levels_.empty()) return;

    // Visible part of the image, in level-0 coordinates.
    const QRectF imgRect(QPointF(0, 0), imageSize());
    const QRectF viewInImg(widgetToImage(QPointF(0, 0)), QSizeF(width(), height()) / scale_);
    const QRectF src = viewInImg.intersected(imgRect);
    if (src.isEmpty()) return;
    const QRectF target((src.topLeft() - viewInImg.topLeft()) * scale_, src.size() * scale_);

    int level = 0;
    const QImage& img = levelFor(scale_ * devicePixelRatioF(), &level);
    const double f = 1 << level;
    // Smooth when minifying; nearest when magnifying so pixels stay visible.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, scale_ * devicePixelRatioF() * f < 1.0);
    painter.drawImage(target, img, QRectF(src.topLeft() / f, src.size() / f));

    if (!viewInImg.contains(imgRect)) paintInset(painter);
}

void ImageView::paintInset(QPainter& painter) {
    const QSizeF img = imageSize();
    const QSizeF insetSize = img.scaled(kInsetMaxSide, kInsetMaxSide, Qt::KeepAspectRatio);
    const QRectF inset(QPointF(width() - kInsetMargin - insetSize.width(), kInsetMargin), insetSize);
    const double insetScale = insetSize.width() / img.width();

    int level = 0;
    const QImage& src = levelFor(insetScale * devicePixelRatioF(), &level);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(0.85);
    painter.drawImage(inset, src);
    painter.setOpacity(1.0);
    painter.setPen(QPen(QColor(255, 255, 255, 160), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(inset.adjusted(-0.5, -0.5, 0.5, 0.5));

    const QRectF viewInImg(widgetToImage(QPointF(0, 0)), QSizeF(width(), height()) / scale_);
    const QRectF view = viewInImg.intersected(QRectF(QPointF(0, 0), img));
    QRectF marker(inset.topLeft() + view.topLeft() * insetScale, view.size() * insetScale);
    // Keep the marker findable at deep zoom.
    const QPointF markerCenter = marker.center();
    marker.setSize(marker.size().expandedTo(QSizeF(6, 6)));
    marker.moveCenter(markerCenter);
    painter.setPen(QPen(QColor(255, 80, 80), 1.5));
    painter.drawRect(marker);
}

void ImageView::resizeEvent(QResizeEvent*) {
    if (fitMode_) {
        fitToView();
    } else {
        clampCenter();
    }
}

void ImageView::wheelEvent(QWheelEvent* e) {
    if (levels_.empty()) return;
    const double notches = e->angleDelta().y() / 120.0;
    if (notches == 0) return;
    const double minScale = std::min(fitScale(), 1.0);
    const double newScale = std::clamp(scale_ * std::pow(kWheelStep, notches), minScale, kMaxScale);
    if (newScale == scale_) return;

    // Keep the image point under the cursor fixed.
    const QPointF anchor = e->position();
    const QPointF anchorImg = widgetToImage(anchor);
    scale_ = newScale;
    center_ = anchorImg - (anchor - QPointF(width() / 2.0, height() / 2.0)) / scale_;
    fitMode_ = false;
    clampCenter();
    update();
}

void ImageView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    dragging_ = true;
    dragStart_ = e->position();
    dragStartCenter_ = center_;
    setCursor(Qt::ClosedHandCursor);
}

void ImageView::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    center_ = dragStartCenter_ - (e->position() - dragStart_) / scale_;
    fitMode_ = false;
    clampCenter();
    update();
}

void ImageView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) fitToView();
}
