For that application, I'd definitely choose Qt 6 Widgets + a pure C++ image-processing engine. And I'd keep Qt out of the processing pipeline entirely.

Your FITS viewer sounds like a specialized scientific image workstation where the priorities are:

1. Extremely fast image display and frame switching.
2. Per-frame adaptive transfer curves and localized gain.
3. Instant blinking between frames, without recomputing anything during playback.
4. Smooth zooming and panning.
5. Low memory overhead and minimal UI complexity.

That is a very different problem from building a modern animated desktop UI. You don't need QML, an embedded browser, or an elaborate UI framework.

## The architecture I'd use

FITS files

16-bit integer / 32-bit float / other supported FITS pixel types

C++ engine

CFITSIO · normalization · localized gain · transfer curves · pyramid generation

Display-ready frame cache

8-bit grayscale / RGBA bitmaps · multiple resolutions · bounded memory

Qt Widgets image viewport

QWidget + QPainter · zoom · pan · blink · loop

Linux

Native build

Windows

Native build

macOS

Native build

The key design principle is that the display thread never processes FITS data. It only selects a prepared bitmap, draws it, and handles user interaction.

## 1. Rendering: start with QPainter, not OpenGL

Qt can display a precomputed `QImage` directly using `QPainter::drawImage()`, including a selected source rectangle and scaling.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://doc.qt.io\&sz=32)

Qt 6.12.0



For example, a minimal viewport:

```
class ImageView : public QWidget {
public:
    using QWidget::QWidget;

    void setFrame(QImage image) {
        frame_ = std::move(image);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        if (!frame_.isNull())
            painter.drawImage(rect(), frame_);
    }

private:
    QImage frame_;
};
```

That's practically all the UI rendering code needed for a first prototype. For production, you'd preserve aspect ratio and implement zoom/pan coordinates.

I'd start with:

- `QImage::Format_Grayscale8` for processed monochrome images.
- `QPainter` for displaying them.
- `QWidget` for the viewport.
- `QTimer` for playback.
- A worker thread pool for preprocessing.

No graphics framework beyond Qt Widgets.

## 2. Optimize for blinking, not just frame rate

For astronomical blinking, what matters is consistent, low-latency switching between prepared images.

I'd separate frame processing and presentation:

| Operation                  | When                                      |
| -------------------------- | ----------------------------------------- |
| FITS decoding              | Asynchronous loading                      |
| Background estimation      | Preprocessing                             |
| Localized gain estimation  | Preprocessing                             |
| Transfer curve computation | Preprocessing                             |
| 8-bit display conversion   | Preprocessing                             |
| Resolution pyramid         | Preprocessing / on demand                 |
| Frame switching            | GUI thread, constant-time frame selection |
| Image presentation         | Qt paint event                            |

A critical detail: don't merely assume `QImage` drawing is free. Even precomputed frames can incur scaling, conversion, copying or upload costs during rendering. Benchmarks will tell us whether that's significant.

For blinking, I'd also ensure each image uses the same viewport coordinates, allowing an optional registration transform when frames aren't already aligned.

## 3. Memory strategy

Take a 4096 × 4096 FITS frame.

Original 16-bit image

# 32 MiB

Processed 8-bit image

# 16 MiB

For 100 frames, full-resolution processed 8-bit images alone require about 1.56 GiB.

A good approach would be to cache images at the displayed resolution, not necessarily the native FITS resolution. A bounded cache with a multiresolution pyramid and prioritized prefetching of the next frames should make blinking fast without consuming excessive RAM.

One consideration specific to your project: if your localized gain depends on spatial neighborhoods, build the lower-resolution representations carefully. Applying local enhancement before versus after downsampling may produce different results.

## 4. When I'd switch to GPU rendering

If QPainter turns out to be a bottleneck, I'd switch the image viewport to a GPU-backed implementation, without changing the rest of the application.

Qt supports `QOpenGLWidget` for this, although OpenGL is deprecated by Apple on macOS. For a longer-lived cross-platform renderer, Qt's QRhi abstraction is worth evaluating because it can work with Metal, Direct3D and Vulkan through suitable backends.

But I wouldn't add that complexity before measuring actual performance.

## 5. What the interface could look like

FITS Blink

[NGC6946 en Cygnus | NOIRLab](https://images.openai.com/static-rsc-4/iSLytHB2GZInn3Ym5bXME8uTxcyOwXXqAvOGr-IhJR1mc-pS-xEiAMwf5f_KudQvj8m6PteJJFw0IpfEr8nap89rh-qymMViy2Dzxuu9-VgAzxgEjO6ccFN0rI2J1Yp6R80KWMnt9zcBmKRQ0Vq6f2V3BbS3hWs2ZJd5xgf8kL0?purpose=inline)

[noirlab.edu](https://noirlab.edu/public/images/noao-02324/)

Frame 012 / 120

4096 × 4096 · 100%

5 FPS

Conceptual UI — a single image viewport, minimal playback controls, and nothing expensive layered over it.

## Final technical choice

| Component      | Choice                    |
| -------------- | ------------------------- |
| Language       | C++20                     |
| GUI            | Qt 6 Widgets              |
| Image viewport | Custom QWidget            |
| Rendering      | QPainter initially        |
| FITS library   | CFITSIO                   |
| Processing     | Pure C++, optional SIMD   |
| Threading      | C++ threads / thread pool |
| Build system   | CMake                     |
| Platforms      | Linux, Windows, macOS     |

The main performance engineering effort should go into your image-processing pipeline, memory layout, caching and frame scheduling — not into the choice of GUI toolkit.

And one architectural decision I'd make immediately: expose processed frames from your C++ engine as a plain pixel buffer with width, height, stride and pixel format. Let a very thin Qt adapter wrap that buffer in `QImage`, with explicit ownership of the underlying memory. That way your entire processing engine remains independent of Qt.

There is one interesting question for this particular application: Do you want the blinking to run at a fixed rate, such as 2–10 frames per second, or do you want high-speed playback at 30–60+ FPS as well?

That could change my choice of renderer and frame-cache architecture.
