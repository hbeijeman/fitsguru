#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>

#include <cstdio>
#include <exception>

#include "fits_image.h"
#include "image_view.h"
#include "stretch.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: fitsguru <file.fits>\n");
        return 2;
    }
    const QString path = args.at(1);

    ImageView view;
    try {
        QElapsedTimer t;
        t.start();
        fg::FitsImage img = fg::loadFits(path.toStdString());
        const qint64 tLoad = t.restart();
        fg::DisplayBuffer buf = fg::autoStretch(img);
        const qint64 tStretch = t.elapsed();
        std::fprintf(stderr, "%dx%d BITPIX=%d%s  load %lld ms, stretch %lld ms\n",
                     img.width, img.height, img.bitpix,
                     img.bayerPattern.empty() ? "" : (" BAYERPAT=" + img.bayerPattern).c_str(),
                     static_cast<long long>(tLoad), static_cast<long long>(tStretch));
        view.setWindowTitle(QStringLiteral("%1 — %2×%3")
                                .arg(QFileInfo(path).fileName())
                                .arg(img.width)
                                .arg(img.height));
        view.setFrame(std::move(buf));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fitsguru: %s\n", e.what());
        QMessageBox::critical(nullptr, "fitsguru", QString::fromStdString(e.what()));
        return 1;
    }

    view.resize(1280, 900);
    view.show();
    return app.exec();
}
