#include "image_view.h"

#include <QPainter>

void ImageView::setFrame(fg::DisplayBuffer buffer) {
    buffer_ = std::move(buffer);
    frame_ = QImage(buffer_.data.data(), buffer_.width, buffer_.height,
                    buffer_.stride, QImage::Format_Grayscale8);
    scaled_ = QImage();
    update();
}

void ImageView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (frame_.isNull()) return;

    const qreal dpr = devicePixelRatioF();
    const QSize target = frame_.size().scaled(size() * dpr, Qt::KeepAspectRatio);
    if (scaled_.size() != target)
        scaled_ = frame_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    const QSizeF logical = QSizeF(target) / dpr;
    const QPointF topLeft((width() - logical.width()) / 2, (height() - logical.height()) / 2);
    painter.drawImage(QRectF(topLeft, logical), scaled_);
}
