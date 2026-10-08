#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <vector>

#include "stretch.h"

// Displays a prepared frame with zoom (wheel) and pan (left drag); while the
// image does not fit the view, an overview inset is shown top-right.
// Never touches FITS data.
class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    // Takes ownership of the pyramid; the QImages wrap its memory without copying.
    // Zoom and pan are kept when the new frame has the same size.
    void setFrame(std::vector<fg::DisplayBuffer> pyramid);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    QSizeF imageSize() const;
    double fitScale() const;
    void fitToView();
    void clampCenter();
    QPointF widgetToImage(QPointF p) const;
    const QImage& levelFor(double devicePixelsPerImagePixel, int* level) const;
    void paintInset(QPainter& painter);

    std::vector<fg::DisplayBuffer> pyramid_;
    std::vector<QImage> levels_;  // views onto pyramid_, level 0 = full resolution

    double scale_ = 1.0;  // widget (logical) pixels per image pixel
    QPointF center_;      // image coordinate shown at the widget centre
    bool fitMode_ = true; // follow window size until the user zooms or pans

    bool dragging_ = false;
    QPointF dragStart_;
    QPointF dragStartCenter_;
};
