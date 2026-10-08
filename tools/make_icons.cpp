// Renders resources/fitsguru.svg into the platform icon files:
//   icons/fitsguru-<N>.png  window icon (embedded as a Qt resource) and Linux hicolor icons
//   fitsguru.ico            Windows executable icon (PNG-compressed entries)
//   fitsguru.icns           macOS bundle icon
// Usage: make_icons <input.svg> <output dir>
// Uses Qt's SVG image plugin, which renders the gradients correctly.

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QtEndian>

#include <cstdio>
#include <map>

namespace {

QByteArray renderPng(const QString& svg, int size) {
    QImageReader reader(svg);
    reader.setScaledSize(QSize(size, size));
    const QImage img = reader.read();
    if (img.isNull()) {
        std::fprintf(stderr, "make_icons: cannot render %s: %s\n", qPrintable(svg),
                     qPrintable(reader.errorString()));
        std::exit(1);
    }
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return png;
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        std::fprintf(stderr, "make_icons: cannot write %s\n", qPrintable(path));
        std::exit(1);
    }
}

template <typename T>
void put(QByteArray& out, T v) {  // little-endian
    v = qToLittleEndian(v);
    out.append(reinterpret_cast<const char*>(&v), sizeof v);
}

template <typename T>
void putBE(QByteArray& out, T v) {
    v = qToBigEndian(v);
    out.append(reinterpret_cast<const char*>(&v), sizeof v);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "usage: make_icons <input.svg> <output dir>\n");
        return 2;
    }
    const QString svg = argv[1];
    const QDir out(argv[2]);
    out.mkpath("icons");

    std::map<int, QByteArray> png;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) png[size] = renderPng(svg, size);

    for (int size : {16, 24, 32, 48, 64, 128, 256, 512})
        writeFile(out.filePath(QStringLiteral("icons/fitsguru-%1.png").arg(size)), png[size]);

    // ICO: header, directory, then PNG blobs (Windows Vista+). 256 is stored as 0.
    const int icoSizes[] = {16, 24, 32, 48, 64, 128, 256};
    const int n = std::size(icoSizes);
    QByteArray ico;
    put<quint16>(ico, 0);
    put<quint16>(ico, 1);  // type: icon
    put<quint16>(ico, n);
    quint32 offset = 6 + 16 * n;
    for (int size : icoSizes) {
        ico.append(char(size >= 256 ? 0 : size));
        ico.append(char(size >= 256 ? 0 : size));
        ico.append(char(0));  // palette
        ico.append(char(0));  // reserved
        put<quint16>(ico, 1);   // planes
        put<quint16>(ico, 32);  // bpp
        put<quint32>(ico, png[size].size());
        put<quint32>(ico, offset);
        offset += png[size].size();
    }
    for (int size : icoSizes) ico.append(png[size]);
    writeFile(out.filePath("fitsguru.ico"), ico);

    // ICNS: big-endian 'icns' + total length, then (type, length, PNG) entries.
    const std::pair<const char*, int> icnsTypes[] = {
        {"icp4", 16},  {"icp5", 32},  {"ic11", 32},  {"icp6", 64},   {"ic12", 64},
        {"ic07", 128}, {"ic08", 256}, {"ic13", 256}, {"ic09", 512},  {"ic14", 512},
        {"ic10", 1024}};
    QByteArray body;
    for (const auto& [type, size] : icnsTypes) {
        body.append(type, 4);
        putBE<quint32>(body, 8 + png[size].size());
        body.append(png[size]);
    }
    QByteArray icns("icns", 4);
    putBE<quint32>(icns, 8 + body.size());
    icns.append(body);
    writeFile(out.filePath("fitsguru.icns"), icns);

    std::fprintf(stderr, "make_icons: wrote icons to %s\n", qPrintable(out.absolutePath()));
    return 0;
}
