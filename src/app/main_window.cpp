#include "main_window.h"

#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeySequence>
#include <QListWidget>
#include <QMenuBar>
#include <QStatusBar>

#include <cstdio>
#include <exception>

#include "image_view.h"
#include "pyramid.h"

MainWindow::MainWindow(QStringList files, const QString& folder, bool debayer)
    : files_(std::move(files)), view_(new ImageView(this)) {
    setCentralWidget(view_);

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
        if (bayer_) render();
    });

    if (files_.size() > 1 || !folder.isEmpty()) {
        fileList_ = new QListWidget;
        for (const QString& f : files_) {
            auto* item = new QListWidgetItem(QFileInfo(f).fileName(), fileList_);
            item->setToolTip(f);
        }
        connect(fileList_, &QListWidget::currentRowChanged, this,
                [this](int row) { if (row >= 0 && row != current_) showFile(row); });

        auto* dock = new QDockWidget(tr("Files (%1)").arg(files_.size()), this);
        dock->setObjectName("files");
        dock->setToolTip(folder);
        dock->setWidget(fileList_);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        resizeDocks({dock}, {260}, Qt::Horizontal);
        viewMenu->addSeparator();
        viewMenu->addAction(dock->toggleViewAction());
    }
}

bool MainWindow::showFile(int index, QString* error) {
    current_ = index;
    if (fileList_) {
        const QSignalBlocker block(fileList_);
        fileList_->setCurrentRow(index);
    }
    const QString path = files_.at(index);
    try {
        QElapsedTimer t;
        t.start();
        image_ = fg::loadFits(path.toStdString());
        std::fprintf(stderr, "%s: %dx%dx%d BITPIX=%d%s  load %lld ms\n",
                     qPrintable(QFileInfo(path).fileName()), image_.width, image_.height,
                     image_.planes, image_.bitpix,
                     image_.bayerPattern.empty() ? "" : (" BAYERPAT=" + image_.bayerPattern).c_str(),
                     static_cast<long long>(t.elapsed()));
    } catch (const std::exception& e) {
        image_ = {};
        bayer_.reset();
        debayerAction_->setEnabled(false);
        view_->setFrame({});
        updateTitle();
        const QString msg = QString::fromStdString(e.what());
        std::fprintf(stderr, "fitsguru: %s\n", qPrintable(msg));
        statusBar()->showMessage(msg);
        if (error) *error = msg;
        return false;
    }

    bayer_ = fg::bayerPattern(image_);
    debayerAction_->setEnabled(bayer_.has_value());
    debayerAction_->setStatusTip(bayer_ ? QString() : tr("No Bayer pattern in this image"));
    if (debayerAction_->isChecked() && !bayer_)
        std::fprintf(stderr, "fitsguru: debayer not applied: image has no usable BAYERPAT\n");
    statusBar()->clearMessage();
    render();
    return true;
}

void MainWindow::render() {
    const bool debayer = debayerAction_->isChecked() && bayer_;
    QElapsedTimer t;
    t.start();
    fg::DisplayBuffer buf = debayer ? fg::autoStretchBayer(image_, *bayer_) : fg::autoStretch(image_);
    const qint64 tStretch = t.restart();
    std::vector<fg::DisplayBuffer> pyramid = fg::buildPyramid(std::move(buf));
    const qint64 tPyramid = t.elapsed();
    std::fprintf(stderr, "%s: stretch %lld ms, pyramid %lld ms (%zu levels)\n",
                 debayer ? "debayer" : "mono", static_cast<long long>(tStretch),
                 static_cast<long long>(tPyramid), pyramid.size());
    view_->setFrame(std::move(pyramid));
    updateTitle();
}

void MainWindow::updateTitle() {
    const QString name = QFileInfo(files_.at(current_)).fileName();
    if (image_.pixels.empty()) {
        setWindowTitle(name);
        return;
    }
    const bool debayer = debayerAction_->isChecked() && bayer_;
    setWindowTitle(QStringLiteral("%1 — %2×%3%4")
                       .arg(name)
                       .arg(image_.width)
                       .arg(image_.height)
                       .arg(debayer ? QStringLiteral(" — RGB (%1)")
                                          .arg(QString::fromStdString(image_.bayerPattern))
                                    : QString()));
}
