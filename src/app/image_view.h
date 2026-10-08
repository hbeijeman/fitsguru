#pragma once

#include <QImage>
#include <QWidget>

#include "stretch.h"

// Displays a prepared frame, aspect-fit to the widget. Never touches FITS data.
class ImageView : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;

    // Takes ownership of the buffer; the QImage wraps its memory without copying.
    void setFrame(fg::DisplayBuffer buffer);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    fg::DisplayBuffer buffer_;
    QImage frame_;
    QImage scaled_;  // cached smooth-downscaled copy for the current widget size
};
