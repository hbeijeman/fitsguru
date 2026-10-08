#include "main_window.h"

#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdio>
#include <exception>

#include "image_view.h"
#include "pyramid.h"

namespace {

constexpr int kPreviewMaxSide = 4096;

using Pyramid = std::vector<fg::DisplayBuffer>;

Pyramid renderPyramid(const fg::FitsImage& img, const std::optional<fg::BayerPattern>& bayer,
                      const fg::StretchCurves& curves) {
    return fg::buildPyramid(bayer ? fg::applyStretchBayer(img, *bayer, curves)
                                  : fg::applyStretch(img, curves));
}

struct PreviewResult {
    std::shared_ptr<const fg::Preview> preview;
    QString error;
};

PreviewResult loadPreviewSafe(const QString& path) {
    try {
        return {std::make_shared<const fg::Preview>(fg::loadPreview(path.toStdString(), kPreviewMaxSide)),
                {}};
    } catch (const std::exception& e) {
        return {nullptr, QString::fromStdString(e.what())};
    }
}

struct FullResult {
    std::shared_ptr<const fg::FitsImage> image;
    std::shared_ptr<Pyramid> pyramid;
    QString error;
};

}  // namespace

MainWindow::MainWindow(QStringList files, const QString& folder, bool debayer)
    : files_(std::move(files)), previews_(files_.size()), view_(new ImageView(this)),
      resolutionLabel_(new QLabel) {
    for (qsizetype i = 0; i < files_.size(); ++i) previewErrors_ << QString();
    previewPool_.setMaxThreadCount(QThread::idealThreadCount());

    setCentralWidget(view_);
    connect(view_, &ImageView::zoomed, this, &MainWindow::requestFullResolution);
    statusBar()->addPermanentWidget(resolutionLabel_);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));  // Cmd+Q on macOS
    quit->setMenuRole(QAction::QuitRole);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    debayerAction_ = viewMenu->addAction(tr("&Debayer"));
    debayerAction_->setCheckable(true);
    debayerAction_->setShortcut(Qt::Key_D);
    debayerAction_->setChecked(debayer);
    debayerAction_->setEnabled(false);
    connect(debayerAction_, &QAction::toggled, this, [this] {
        if (preview_ && preview_->bayer) render();
    });

    if (files_.size() > 1 || !folder.isEmpty()) {
        fileList_ = new QListWidget;
        for (const QString& f : files_) {
            auto* item = new QListWidgetItem(QFileInfo(f).fileName(), fileList_);
            item->setToolTip(f);
        }
        for (int i = 0; i < fileList_->count(); ++i) updateFileItem(i);
        connect(fileList_, &QListWidget::currentRowChanged, this,
                [this](int row) { if (row >= 0 && row != current_) showFile(row); });

        dock_ = new QDockWidget(this);
        dock_->setObjectName("files");
        dock_->setToolTip(folder);
        dock_->setWidget(fileList_);
        updateDockTitle();
        addDockWidget(Qt::RightDockWidgetArea, dock_);
        resizeDocks({dock_}, {260}, Qt::Horizontal);
        viewMenu->addSeparator();
        viewMenu->addAction(dock_->toggleViewAction());

        // After the first showFile(), so the initially shown file is skipped.
        QTimer::singleShot(0, this, &MainWindow::startPreloading);
    }
}

MainWindow::~MainWindow() {
    previewPool_.clear();  // drop queued jobs, wait only for running ones
    previewPool_.waitForDone();
}

void MainWindow::startPreloading() {
    QElapsedTimer* timer = new QElapsedTimer;
    timer->start();
    auto remaining = std::make_shared<int>(0);
    for (int i = 0; i < files_.size(); ++i) {
        if (previews_[i] || !previewErrors_[i].isEmpty()) continue;
        ++*remaining;
        auto* watcher = new QFutureWatcher<PreviewResult>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, i, remaining, timer] {
            watcher->deleteLater();
            const PreviewResult r = watcher->result();
            storePreview(i, r.preview, r.error);
            if (--*remaining == 0) {
                size_t bytes = 0;
                for (const auto& p : previews_)
                    if (p) bytes += p->image.pixels.size() * sizeof(float);
                std::fprintf(stderr, "previews: %lld files in %lld ms on %d threads, %.0f MiB\n",
                             static_cast<long long>(files_.size()),
                             static_cast<long long>(timer->elapsed()),
                             previewPool_.maxThreadCount(), bytes / 1048576.0);
                delete timer;
            }
        });
        watcher->setFuture(QtConcurrent::run(&previewPool_, loadPreviewSafe, files_.at(i)));
    }
    if (*remaining == 0) delete timer;
}

void MainWindow::storePreview(int index, std::shared_ptr<const fg::Preview> preview,
                              const QString& error) {
    if (previews_[index] || !previewErrors_[index].isEmpty()) return;  // already built on demand
    previews_[index] = std::move(preview);
    previewErrors_[index] = error;
    updateFileItem(index);
    updateDockTitle();
}

// Grey while the preview is pending, red if the file could not be read.
void MainWindow::updateFileItem(int index) {
    if (!fileList_) return;
    QListWidgetItem* item = fileList_->item(index);
    const QString& error = previewErrors_[index];
    if (!error.isEmpty()) {
        item->setForeground(Qt::red);
        item->setToolTip(files_.at(index) + "\n" + error);
    } else if (!previews_[index]) {
        item->setForeground(fileList_->palette().color(QPalette::PlaceholderText));
    } else {
        item->setData(Qt::ForegroundRole, QVariant());
        const auto& p = *previews_[index];
        item->setToolTip(QStringLiteral("%1\n%2×%3%4")
                             .arg(files_.at(index))
                             .arg(p.fullWidth)
                             .arg(p.fullHeight)
                             .arg(p.bayer ? QStringLiteral(" ") +
                                                QString::fromStdString(p.image.bayerPattern)
                                          : QString()));
    }
}

void MainWindow::updateDockTitle() {
    if (!dock_) return;
    int done = 0;
    for (int i = 0; i < files_.size(); ++i)
        if (previews_[i] || !previewErrors_[i].isEmpty()) ++done;
    dock_->setWindowTitle(done == files_.size()
                              ? tr("Files (%1)").arg(files_.size())
                              : tr("Files — previews %1/%2").arg(done).arg(files_.size()));
}

bool MainWindow::debayering() const {
    return debayerAction_->isChecked() && preview_ && preview_->bayer;
}

void MainWindow::showError(const QString& msg, QString* error) {
    preview_.reset();
    fullImage_.reset();
    ++generation_;
    fullShown_ = false;
    debayerAction_->setEnabled(false);
    view_->setFrame({});
    updateTitle();
    updateResolutionLabel();
    std::fprintf(stderr, "fitsguru: %s\n", qPrintable(msg));
    statusBar()->showMessage(msg);
    if (error) *error = msg;
}

bool MainWindow::showFile(int index, QString* error) {
    current_ = index;
    if (fileList_) {
        const QSignalBlocker block(fileList_);
        fileList_->setCurrentRow(index);
    }
    fullImage_.reset();

    if (!previews_[index] && previewErrors_[index].isEmpty()) {
        // Not built by the pool yet: do it now rather than wait in its queue.
        QElapsedTimer t;
        t.start();
        PreviewResult r = loadPreviewSafe(files_.at(index));
        if (r.preview)
            std::fprintf(stderr, "%s: preview built on demand in %lld ms\n",
                         qPrintable(QFileInfo(files_.at(index)).fileName()),
                         static_cast<long long>(t.elapsed()));
        storePreview(index, std::move(r.preview), r.error);
    }
    if (!previewErrors_[index].isEmpty()) {
        showError(previewErrors_[index], error);
        return false;
    }

    preview_ = previews_[index];
    const bool hasBayer = preview_->bayer.has_value();
    debayerAction_->setEnabled(hasBayer);
    debayerAction_->setStatusTip(hasBayer ? QString() : tr("No Bayer pattern in this image"));
    if (debayerAction_->isChecked() && !hasBayer)
        std::fprintf(stderr, "fitsguru: debayer not applied: image has no usable BAYERPAT\n");
    statusBar()->clearMessage();
    render();
    return true;
}

// Shows the preview (which is the image itself when it is small enough) right
// away; the full-resolution frame follows if the view is zoomed.
void MainWindow::render() {
    ++generation_;
    const fg::Preview& p = *preview_;
    fullShown_ = p.step == 1;
    const auto bayer = debayering() ? p.bayer : std::nullopt;

    QElapsedTimer t;
    t.start();
    Pyramid pyramid = renderPyramid(p.image, bayer, bayer ? *p.bayerCurves : p.monoCurves);
    std::fprintf(stderr, "%s: %s %s render %lld ms\n",
                 qPrintable(QFileInfo(files_.at(current_)).fileName()),
                 p.step > 1 ? "preview" : "full", bayer ? "debayer" : "mono",
                 static_cast<long long>(t.elapsed()));
    view_->setFrame(std::move(pyramid), QSize(p.fullWidth, p.fullHeight), p.step);
    updateTitle();
    updateResolutionLabel();

    if (view_->isZoomed()) requestFullResolution();
}

void MainWindow::requestFullResolution() {
    if (!preview_ || fullShown_ || fullRequested_ == generation_) return;
    fullRequested_ = generation_;
    updateResolutionLabel();

    const int generation = generation_;
    auto* watcher = new QFutureWatcher<FullResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != generation_) return;  // superseded
        FullResult r = watcher->result();
        if (!r.error.isEmpty()) {
            statusBar()->showMessage(r.error);
            std::fprintf(stderr, "fitsguru: %s\n", qPrintable(r.error));
            return;
        }
        fullImage_ = std::move(r.image);
        view_->setFrame(std::move(*r.pyramid), QSize(preview_->fullWidth, preview_->fullHeight), 1);
        fullShown_ = true;
        updateResolutionLabel();
    });
    // The worker keeps its own references, so switching files mid-render is safe.
    const auto bayer = debayering() ? preview_->bayer : std::nullopt;
    watcher->setFuture(QtConcurrent::run(
        [path = files_.at(current_), image = fullImage_, bayer,
         curves = bayer ? *preview_->bayerCurves : preview_->monoCurves]() -> FullResult {
            try {
                QElapsedTimer t;
                t.start();
                auto img = image ? image
                                 : std::make_shared<const fg::FitsImage>(fg::loadFits(path.toStdString()));
                const qint64 tLoad = t.restart();
                auto pyramid = std::make_shared<Pyramid>(renderPyramid(*img, bayer, curves));
                std::fprintf(stderr, "full %s: load %lld ms, render %lld ms (background)\n",
                             bayer ? "debayer" : "mono", static_cast<long long>(tLoad),
                             static_cast<long long>(t.elapsed()));
                return {img, pyramid, {}};
            } catch (const std::exception& e) {
                return {nullptr, nullptr, QString::fromStdString(e.what())};
            }
        }));
}

void MainWindow::updateResolutionLabel() {
    if (!preview_ || preview_->step == 1) {
        resolutionLabel_->clear();
    } else if (fullShown_) {
        resolutionLabel_->setText(tr("Full resolution"));
    } else if (fullRequested_ == generation_) {
        resolutionLabel_->setText(tr("Preview 1:%1 — loading full resolution…").arg(preview_->step));
    } else {
        resolutionLabel_->setText(tr("Preview 1:%1 — zoom for full resolution").arg(preview_->step));
    }
}

void MainWindow::updateTitle() {
    const QString name = QFileInfo(files_.at(current_)).fileName();
    if (!preview_) {
        setWindowTitle(name);
        return;
    }
    setWindowTitle(QStringLiteral("%1 — %2×%3%4")
                       .arg(name)
                       .arg(preview_->fullWidth)
                       .arg(preview_->fullHeight)
                       .arg(debayering() ? QStringLiteral(" — RGB (%1)")
                                               .arg(QString::fromStdString(preview_->image.bayerPattern))
                                         : QString()));
}
