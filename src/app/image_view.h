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
    // `imageSize` is the full-resolution size the view works in (default: the
    // pyramid's base level); `step` is how many full-resolution pixels each
    // base-level pixel stands for (> 1 for a decimated preview). Zoom and pan
    // are kept when the new frame has the same imageSize.
    void setFrame(std::vector<fg::DisplayBuffer> pyramid, QSize imageSize = {}, int step = 1);

    // True once the user has zoomed or panned away from fit-to-window.
    bool isZoomed() const { return !fitMode_; }

signals:
    // The user changed the zoom level.
    void zoomed();

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
    std::vector<QImage> levels_;  // views onto pyramid_, level 0 = base
    QSize imageSize_;             // full-resolution size, in view coordinates
    int step_ = 1;                // full-resolution pixels per base-level pixel

    double scale_ = 1.0;  // widget (logical) pixels per image pixel
    QPointF center_;      // image coordinate shown at the widget centre
    bool fitMode_ = true; // follow window size until the user zooms or pans

    bool dragging_ = false;
    QPointF dragStart_;
    QPointF dragStartCenter_;
};
