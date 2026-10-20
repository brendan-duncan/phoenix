#include "gpu_bakeoff.h"

#include "gpu_device.h"
#include "vector_backend.h"

#include "../gui/phoenix_view.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

QString option(const QStringList& options, const QString& name)
{
    const int at = static_cast<int>(options.indexOf(name));
    return at >= 0 && at + 1 < options.size() ? options.at(at + 1) : QString();
}

/// How far \a image is from \a reference: the PSNR over the color channels
/// (higher is closer), the mean difference and how many pixels are off by
/// more than 16 of 255.
struct Error
{
    double psnr = 0.0;
    double mean = 0.0;
    double off = 0.0;
};

Error compare(const QImage& image, const QImage& reference)
{
    Error error;
    if (image.size() != reference.size())
        return error;
    double squares = 0.0;
    double sum = 0.0;
    qint64 off = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const auto* a = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        const auto* b = reinterpret_cast<const QRgb*>(reference.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const int dr = qRed(a[x]) - qRed(b[x]);
            const int dg = qGreen(a[x]) - qGreen(b[x]);
            const int db = qBlue(a[x]) - qBlue(b[x]);
            squares += dr * dr + dg * dg + db * db;
            sum += std::abs(dr) + std::abs(dg) + std::abs(db);
            if (std::max({std::abs(dr), std::abs(dg), std::abs(db)}) > 16)
                ++off;
        }
    }
    const double count = static_cast<double>(image.width()) * image.height();
    const double mse = squares / (3.0 * count);
    error.psnr = mse > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0;
    error.mean = sum / (3.0 * count);
    error.off = off / count;
    return error;
}

/// Where \a image differs from \a reference, ten times as strong, on black.
QImage difference(const QImage& image, const QImage& reference)
{
    QImage out(reference.size(), QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y)
    {
        const auto* a = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        const auto* b = reinterpret_cast<const QRgb*>(reference.constScanLine(y));
        auto* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
        {
            const auto amp = [](int v) { return std::min(255, std::abs(v) * 10); };
            d[x] = qRgb(amp(qRed(a[x]) - qRed(b[x])), amp(qGreen(a[x]) - qGreen(b[x])), amp(qBlue(a[x]) - qBlue(b[x])));
        }
    }
    return out;
}

QImage scaledDown(const QImage& image, const QSize& size)
{
    return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

/// The recorded items drawn again with QPainter, to tell what the recording
/// loses from what a GPU renderer does.
QImage replay(const std::vector<RecordedItem>& items, const QSize& size, const QColor& clear)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(clear);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const RecordedItem& item : items)
    {
        painter.save();
        painter.setOpacity(item.opacity);
        painter.setCompositionMode(item.composition);
        if (item.clipped)
            painter.setClipPath(item.clip);
        switch (item.kind)
        {
        case RecordedItem::Kind::Fill:
            painter.fillPath(item.path, item.brush);
            break;
        case RecordedItem::Kind::Stroke:
        {
            QPen pen = item.pen;
            pen.setBrush(item.brush);
            painter.strokePath(item.path, pen);
            break;
        }
        case RecordedItem::Kind::Image:
            painter.setTransform(item.imageTransform);
            painter.drawImage(0, 0, item.image);
            break;
        case RecordedItem::Kind::Text:
            break;
        }
        painter.restore();
    }
    return image;
}

} // namespace

int runGpuBakeoff(PhoenixView& view, const QSize& documentSize, int frameCount, const QStringList& options)
{
    QString why;
    GpuDevice* device = GpuDevice::get(&why);
    if (!device)
    {
        std::fprintf(stderr, "gpu-bakeoff: no GPU device: %s\n", qPrintable(why));
        return 1;
    }
    std::printf("GPU: %s\n", qPrintable(device->description()));

    const QDir out(option(options, "--out").isEmpty() ? QStringLiteral("gpu-bakeoff") : option(options, "--out"));
    QDir().mkpath(out.path());
    const int width = option(options, "--width").isEmpty() ? documentSize.width() : option(options, "--width").toInt();
    const QSize size(width, std::max(1, qRound(static_cast<double>(width) * documentSize.height() / documentSize.width())));
    const int repeat = std::max(1, option(options, "--repeat").isEmpty() ? 3 : option(options, "--repeat").toInt());
    std::vector<int> frames;
    if (!option(options, "--frames").isEmpty())
    {
        for (const QString& each : option(options, "--frames").split(','))
            frames.push_back(each.toInt());
    }
    else
    {
        const int count = std::max(1, option(options, "--count").isEmpty() ? 4 : option(options, "--count").toInt());
        for (int i = 0; i < count; ++i)
            frames.push_back(frameCount * i / count);
    }

    std::vector<std::unique_ptr<VectorBackend>> backends;
    backends.push_back(makeRiveBackend(*device));

    // Each way of drawing: its name, and how it draws a frame (the time it
    // takes reported through the timing).
    struct Way
    {
        QString name;
        std::function<QImage(int frame, QJsonObject& timing)> draw;
    };
    std::vector<Way> ways;
    const QColor clear = [&view]() {
        const QImage probe = view.renderFrame(0, QSize(1, 1), false);
        return QColor::fromRgba(probe.pixel(0, 0));
    }();
    const auto timed = [](QJsonObject& timing, const char* key, const std::function<void()>& work) {
        QElapsedTimer clock;
        clock.start();
        work();
        timing[key] = clock.nsecsElapsed() / 1.0e6;
    };
    ways.push_back({"qpainter", [&](int frame, QJsonObject& timing) {
                        QImage image;
                        timed(timing, "total", [&]() { image = view.renderFrame(frame, size, false); });
                        return image;
                    }});
    ways.push_back({"qpainter-2x", [&](int frame, QJsonObject& timing) {
                        QImage image;
                        timed(timing, "total", [&]() { image = scaledDown(view.renderFrame(frame, size * 2, false), size); });
                        return image;
                    }});
    ways.push_back({"replay", [&](int frame, QJsonObject& timing) {
                        std::vector<RecordedItem> items;
                        timed(timing, "record", [&]() { items = view.recordFrame(frame, size, true); });
                        QImage image;
                        timed(timing, "total", [&]() { image = replay(items, size, clear); });
                        return image;
                    }});
    for (const std::unique_ptr<VectorBackend>& backend : backends)
        for (const int scale : {1, 2})
        {
            VectorBackend* drawer = backend.get();
            ways.push_back({drawer->name().toLower() + (scale == 2 ? "-2x" : ""), [&, drawer, scale](int frame, QJsonObject& timing) {
                                std::vector<RecordedItem> items;
                                timed(timing, "record", [&]() { items = view.recordFrame(frame, size * scale, true); });
                                VectorBackend::Timing parts;
                                QImage image = drawer->render(items, size * scale, clear, parts);
                                timing["encode"] = parts.encodeMs;
                                timing["gpu"] = parts.gpuMs;
                                timing["read"] = parts.readMs;
                                timing["total"] = parts.encodeMs + parts.gpuMs;
                                timing["items"] = static_cast<int>(items.size());
                                if (scale != 1)
                                    image = scaledDown(image, size);
                                return image;
                            }});
        }

    QJsonObject report;
    report["gpu"] = device->description();
    report["width"] = size.width();
    report["height"] = size.height();
    QJsonArray frameReports;
    std::printf("%dx%d, %d frames, best of %d\n", size.width(), size.height(), static_cast<int>(frames.size()), repeat);
    for (const int frame : frames)
    {
        // The reference: QPainter at four times the size, scaled down.
        const QImage reference = scaledDown(view.renderFrame(frame, size * 4, false), size);
        reference.save(out.filePath(QString("f%1-reference.png").arg(frame)));
        QJsonObject frameReport;
        frameReport["frame"] = frame;
        std::printf("frame %d\n", frame);
        for (const Way& way : ways)
        {
            QImage image;
            QJsonObject best;
            for (int i = 0; i < repeat; ++i)
            {
                QJsonObject timing;
                image = way.draw(frame, timing);
                if (best.isEmpty() || timing["total"].toDouble() < best["total"].toDouble())
                    best = timing;
            }
            const Error error = compare(image, reference);
            best["psnr"] = error.psnr;
            best["mean"] = error.mean;
            best["off"] = error.off;
            frameReport[way.name] = best;
            image.save(out.filePath(QString("f%1-%2.png").arg(frame).arg(way.name)));
            difference(image, reference).save(out.filePath(QString("f%1-%2-diff.png").arg(frame).arg(way.name)));
            std::printf("  %-12s %8.2f ms (record %6.2f, encode %6.2f, gpu %6.2f)  PSNR %5.1f dB  mean %5.2f  off %5.2f%%\n",
                qPrintable(way.name), best["total"].toDouble(), best["record"].toDouble(), best["encode"].toDouble(),
                best["gpu"].toDouble(), error.psnr, error.mean, error.off * 100.0);
        }
        frameReports.append(frameReport);
    }
    report["frames"] = frameReports;
    QJsonObject unsupported;
    for (const std::unique_ptr<VectorBackend>& backend : backends)
    {
        QJsonObject missing;
        for (auto it = backend->unsupported.cbegin(); it != backend->unsupported.cend(); ++it)
            missing[it.key()] = it.value();
        unsupported[backend->name()] = missing;
        for (auto it = backend->unsupported.cbegin(); it != backend->unsupported.cend(); ++it)
            std::printf("%s could not: %s (%d)\n", qPrintable(backend->name()), qPrintable(it.key()), it.value());
    }
    report["unsupported"] = unsupported;
    QFile file(out.filePath("report.json"));
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(report).toJson());
    return 0;
}
