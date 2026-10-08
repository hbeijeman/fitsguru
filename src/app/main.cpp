#include <QApplication>
#include <QCollator>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "main_window.h"

namespace {

[[noreturn]] void fail(const QString& msg) {
    std::fprintf(stderr, "fitsguru: %s\n", qPrintable(msg));
    QMessageBox::critical(nullptr, "fitsguru", msg);
    std::exit(1);
}

// FITS files directly inside `dir` (extensions .fits/.fit/.fts, any case),
// in natural order so frame_2 sorts before frame_10.
QStringList fitsFilesIn(const QString& dir) {
    QStringList files;
    const QFileInfoList entries =
        QDir(dir).entryInfoList({"*.fits", "*.fit", "*.fts"}, QDir::Files | QDir::Readable);
    for (const QFileInfo& fi : entries) files << fi.absoluteFilePath();
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(files.begin(), files.end(), collator);
    return files;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("fitsguru");

    QCommandLineParser parser;
    parser.setApplicationDescription("FITS image viewer");
    parser.addHelpOption();
    const QCommandLineOption debayerOption(
        {"debayer", "rgb"}, "Demosaic single-plane images that have a BAYERPAT header.");
    parser.addOption(debayerOption);
    parser.addPositionalArgument(
        "path", "FITS file to display, or a folder of .fits/.fit/.fts files to browse.");
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);
    const QFileInfo arg(parser.positionalArguments().first());

    QStringList files;
    QString folder;
    if (arg.isDir()) {
        folder = arg.absoluteFilePath();
        files = fitsFilesIn(folder);
        if (files.isEmpty()) fail(QStringLiteral("no .fits/.fit/.fts files in '%1'").arg(folder));
        std::fprintf(stderr, "%s: %lld FITS files\n", qPrintable(folder),
                     static_cast<long long>(files.size()));
    } else if (arg.exists()) {
        files << arg.absoluteFilePath();
    } else {
        fail(QStringLiteral("'%1' does not exist").arg(arg.filePath()));
    }

    MainWindow window(files, folder, parser.isSet(debayerOption));
    window.resize(folder.isEmpty() ? 1280 : 1540, 900);
    QString error;
    // A single unreadable file is fatal; in a folder the user can pick another.
    if (!window.showFile(0, &error) && folder.isEmpty()) fail(error);
    window.show();
    return app.exec();
}
