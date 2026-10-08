#pragma once

#include <QMainWindow>
#include <QStringList>

#include <optional>

#include "fits_image.h"
#include "stretch.h"

class ImageView;
class QAction;
class QListWidget;

// Owns the loaded FITS data and turns it into display frames for ImageView.
// With more than one file (a folder was opened) a file list is docked on the
// right; selecting an entry loads it.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // `folder` is shown as the pane title; empty for a single file.
    MainWindow(QStringList files, const QString& folder, bool debayer);

    // Loads and displays files[index]. On failure the view is cleared and the
    // error is reported (stderr + status bar); returns false.
    bool showFile(int index, QString* error = nullptr);

private:
    void render();
    void updateTitle();

    QStringList files_;
    int current_ = -1;
    fg::FitsImage image_;
    std::optional<fg::BayerPattern> bayer_;
    ImageView* view_;
    QAction* debayerAction_;  // checked = user preference, enabled = applicable
    QListWidget* fileList_ = nullptr;
};
