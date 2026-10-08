#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QThreadPool>

#include <memory>
#include <vector>

#include "fits_image.h"
#include "preview.h"

class ImageView;
class QAction;
class QDockWidget;
class QLabel;
class QListWidget;

// Owns the loaded FITS data and turns it into display frames for ImageView.
// With more than one file (a folder was opened) a file list is docked on the
// right; selecting an entry shows it.
//
// Every file is first reduced to a preview (larger side <= 4096 px). For a
// folder, previews of all files are built in the background on a thread pool
// (one thread per CPU) and kept in memory, so switching files only has to
// stretch an in-memory preview. The full-resolution frame is loaded and
// rendered in the background once the user zooms (or immediately, if the view
// is already zoomed when the file is selected).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // `folder` is shown as the pane title; empty for a single file.
    MainWindow(QStringList files, const QString& folder, bool debayer);
    ~MainWindow() override;

    // Displays files[index], building its preview now if the pool has not got
    // to it yet. On failure the view is cleared and the error is reported
    // (stderr + status bar); returns false.
    bool showFile(int index, QString* error = nullptr);

private:
    void startPreloading();
    void storePreview(int index, std::shared_ptr<const fg::Preview> preview, const QString& error);
    void updateFileItem(int index);
    void updateDockTitle();
    void showError(const QString& msg, QString* error);
    void render();
    void requestFullResolution();
    void updateTitle();
    void updateResolutionLabel();
    bool debayering() const;

    QStringList files_;
    std::vector<std::shared_ptr<const fg::Preview>> previews_;  // per file; null until built
    QStringList previewErrors_;                                 // per file; empty if fine
    QThreadPool previewPool_;

    int current_ = -1;
    std::shared_ptr<const fg::Preview> preview_;    // of the current file
    std::shared_ptr<const fg::FitsImage> fullImage_;  // of the current file, once loaded

    // Full-resolution rendering state. Bumped on every render() so results
    // from an outdated request are dropped.
    int generation_ = 0;
    int fullRequested_ = -1;  // generation a full render was started for
    bool fullShown_ = false;

    ImageView* view_;
    QAction* debayerAction_;  // checked = user preference, enabled = applicable
    QListWidget* fileList_ = nullptr;
    QDockWidget* dock_ = nullptr;
    QLabel* resolutionLabel_;
};
