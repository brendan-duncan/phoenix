#include "html5_publish.h"

#include "audio_engine.h"
#include "movie_export.h"
#include "paint_recorder.h"
#include "phoenix_view.h"
#include "timeline_audio.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/symbol.h"
#include "../edit/canvas_document.h"
#include "../edit/html5_actionscript2.h"
#include "../edit/html5_export.h"
#include "../edit/swf_actionscript3.h"
#include "../play/script/class_path.h"
#include "movie_hooks.h"

#include "js/js.h"

#include "tinyxml2.h"

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QPainter>
#include <QPainterPathStroker>
#include "process.h"
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <map>

namespace Html5Publish {

namespace {

using fla::html5::number;

/// The runtime's files, in the order they build on each other.
const char* const kRuntimeFiles[] = {"core", "geom", "events", "display", "graphics", "filters", "render", "bitmap",
    "timeline", "text", "media", "utils", "player"};

QByteArray resource(const QString& name)
{
    QFile file(":/html5/" + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

std::string matrixJson(const QTransform& t)
{
    return "[" + number(t.m11(), 5) + "," + number(t.m12(), 5) + "," + number(t.m21(), 5) + "," + number(t.m22(), 5) +
        "," + number(t.dx(), 3) + "," + number(t.dy(), 3) + "]";
}

/// A path as SVG path data, which the runtime hands to Path2D.
std::string pathData(const QPainterPath& path)
{
    std::string out;
    out.reserve(static_cast<size_t>(path.elementCount()) * 14);
    QPointF start;
    QPointF at;
    for (int i = 0; i < path.elementCount(); ++i)
    {
        const QPainterPath::Element& e = path.elementAt(i);
        if (e.type == QPainterPath::MoveToElement)
        {
            start = at = QPointF(e.x, e.y);
            out += "M" + number(e.x, 2) + " " + number(e.y, 2);
        }
        else if (e.type == QPainterPath::LineToElement)
        {
            at = QPointF(e.x, e.y);
            out += "L" + number(e.x, 2) + " " + number(e.y, 2);
        }
        else if (e.type == QPainterPath::CurveToElement && i + 2 < path.elementCount())
        {
            const QPainterPath::Element& c2 = path.elementAt(i + 1);
            const QPainterPath::Element& end = path.elementAt(i + 2);
            out += "C" + number(e.x, 2) + " " + number(e.y, 2) + " " + number(c2.x, 2) + " " + number(c2.y, 2) + " " +
                number(end.x, 2) + " " + number(end.y, 2);
            at = QPointF(end.x, end.y);
            i += 2;
        }
        // An outline back where it began is closed, so its corner joins.
        const bool last = i + 1 >= path.elementCount() || path.elementAt(i + 1).type == QPainterPath::MoveToElement;
        if (last && i > 0 && at == start && e.type != QPainterPath::MoveToElement)
            out += "Z";
    }
    return out;
}

std::string boxJson(const QRectF& box)
{
    return "[" + number(box.left(), 2) + "," + number(box.top(), 2) + "," + number(box.right(), 2) + "," +
        number(box.bottom(), 2) + "]";
}

const char* blendOf(QPainter::CompositionMode mode)
{
    switch (mode)
    {
    case QPainter::CompositionMode_Multiply: return "multiply";
    case QPainter::CompositionMode_Screen: return "screen";
    case QPainter::CompositionMode_Lighten: return "lighten";
    case QPainter::CompositionMode_Darken: return "darken";
    case QPainter::CompositionMode_Difference: return "difference";
    case QPainter::CompositionMode_Plus: return "add";
    case QPainter::CompositionMode_Overlay: return "overlay";
    case QPainter::CompositionMode_HardLight: return "hardlight";
    case QPainter::CompositionMode_DestinationIn: return "alpha";
    case QPainter::CompositionMode_DestinationOut: return "erase";
    default: return nullptr;
    }
}

/// The art source: each element drawn through the view's own renderer,
/// recorded, and written as the runtime's shape JSON; the pictures and
/// sounds it uses written beside the page.
class ViewArt : public fla::html5::ArtSource
{
public:
    ViewArt(PhoenixView& view, TimelineAudio* audio, const fla::Document& document, const QDir& folder, const QString& assets)
        : _view(view)
        , _audio(audio)
        , _document(document)
        , _folder(folder)
        , _assets(assets)
    {
        // The library's sounds, by name, and their linkage.
        for (const std::string& item : document.extraMedia)
        {
            if (item.find("<DOMSoundItem") == std::string::npos)
                continue;
            tinyxml2::XMLDocument xml;
            if (xml.Parse(item.c_str(), item.size()) != tinyxml2::XML_SUCCESS || !xml.FirstChildElement())
                continue;
            const tinyxml2::XMLElement* sound = xml.FirstChildElement();
            const char* name = sound->Attribute("name");
            const char* linkage = sound->Attribute("linkageClassName");
            if (name && linkage && *linkage)
                _soundLinkage[name] = linkage;
        }
    }

    QStringList written;
    QStringList notes;

    /// The sounds scripts make by their linkage, whether or not a frame
    /// plays them.
    void linkedSounds()
    {
        for (const auto& [name, linkage] : _soundLinkage)
            sound(name);
    }

    std::string art(const fla::Element& element) override
    {
        const std::vector<RecordedItem> items = _view.recordElement(&element);
        std::string ops;
        QRectF box;
        for (const RecordedItem& item : items)
        {
            std::string op = opJson(item, box);
            if (op.empty())
                continue;
            ops += (ops.empty() ? "" : ",") + op;
        }
        if (ops.empty() || !box.isValid())
            return std::string();
        return "{\"o\":[" + ops + "],\"b\":" + boxJson(box) + "}";
    }

    int bitmap(const fla::Bitmap& bitmap) override
    {
        const QImage image = _view.libraryBitmap(QString::fromStdString(bitmap.name)).toImage();
        if (image.isNull())
            return -1;
        std::string linkage;
        for (const auto& [key, value] : bitmap.extraAttributes)
            if (key == "linkageClassName")
                linkage = value;
        return imageIndex(image, bitmap.name, linkage);
    }

    int sound(const std::string& name) override
    {
        const auto known = _soundIndex.find(name);
        if (known != _soundIndex.end())
            return known->second;
        int index = -1;
        std::shared_ptr<const PcmBuffer> pcm = _audio ? _audio->librarySound(name) : nullptr;
        if (pcm && !pcm->samples.empty())
        {
            const int n = static_cast<int>(_sounds.size());
            QString file;
            QByteArray bytes;
            // MP3, or AAC where Phoenix's ffmpeg has no MP3 encoder: both
            // play in every current browser. WAV only without ffmpeg.
            std::vector<uint8_t> encoded;
            QString encodeError;
            if (MovieExport::canEncodeMp3() && MovieExport::encodeMp3(pcm->samples, 44100, 2, 128, encoded, encodeError))
                file = QString("%1/sound%2.mp3").arg(_assets).arg(n);
            else if (MovieExport::encodeAac(pcm->samples, 44100, 2, 128, encoded, encodeError))
                file = QString("%1/sound%2.m4a").arg(_assets).arg(n);
            if (!file.isEmpty())
                bytes = QByteArray(reinterpret_cast<const char*>(encoded.data()), static_cast<qsizetype>(encoded.size()));
            else
            {
                file = QString("%1/sound%2.wav").arg(_assets).arg(n);
                bytes = wav(*pcm);
                const QString why = MovieExport::ffmpegPath().isEmpty()
                    ? QString("this Phoenix is missing its ffmpeg")
                    : QString("ffmpeg could not encode them (%1)").arg(encodeError);
                const QString note = QString("Sounds are written as WAV, much larger than MP3: %1.").arg(why);
                if (!notes.contains(note))
                    notes << note;
            }
            if (write(file, bytes))
            {
                index = n;
                std::string entry = "{\"src\":" + fla::html5::quoted(file.toStdString()) + ",\"name\":" + fla::html5::quoted(name);
                const auto linkage = _soundLinkage.find(name);
                if (linkage != _soundLinkage.end())
                    entry += ",\"linkage\":" + fla::html5::quoted(linkage->second);
                _sounds.push_back(entry + "}");
            }
        }
        _soundIndex[name] = index;
        return index;
    }

    std::string picture(const fla::Timeline& timeline, int from, int to, int frame) override
    {
        // A scene's stage, as a published movie shows it, or what a
        // symbol's frame covers with room for the effects' reach.
        const auto& scenes = _document.timelines;
        const auto scene = std::find(scenes.begin(), scenes.end(), &timeline);
        QRectF area;
        if (scene != scenes.end())
            area = QRectF(0, 0, std::max(1, _document.width), std::max(1, _document.height));
        else if (const auto* symbol = dynamic_cast<const fla::Symbol*>(timeline.parent))
            area = _view.symbolFramesBounds(symbol, frame, frame).adjusted(-64, -64, 64, 64);
        if (!area.isValid() || area.isEmpty())
            return std::string();
        // Sharper than the stage's pixels, up to a size that stays small.
        const double scale = std::clamp(1280.0 / std::max(area.width(), area.height()), 1.0, 2.0);
        // A scene is drawn as the one shown, which its camera and motion
        // blur work on.
        auto& document = const_cast<fla::Document&>(_document);
        const std::string shownScene = document.currentTimeline;
        const bool cameraView = _view.cameraView();
        if (scene != scenes.end())
            document.setCurrentScene(static_cast<int>(scene - scenes.begin()));
        _view.setCameraView(true);
        const QImage image = _view.renderTimelinePart(&timeline, from, to, nullptr, frame, area, scale);
        _view.setCameraView(cameraView);
        document.currentTimeline = shownScene;
        if (image.isNull())
            return std::string();
        // Only as far as something shows.
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(line[x]) != 0)
                {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        }
        if (right < 0)
            return std::string();
        const QImage cropped = image.copy(left, top, right - left + 1, bottom - top + 1);
        const int index = frameIndex(cropped);
        if (index < 0)
            return std::string();
        const QTransform place(1.0 / scale, 0.0, 0.0, 1.0 / scale, area.left() + left / scale, area.top() + top / scale);
        const QRectF box = place.mapRect(QRectF(0, 0, cropped.width(), cropped.height()));
        return "{\"o\":[{\"i\":" + std::to_string(index) + ",\"m\":" + matrixJson(place) + ",\"b\":" + boxJson(box) +
            "}],\"b\":" + boxJson(box) + "}";
    }

    std::string images() override
    {
        packFrames();
        std::string out;
        for (const std::string& image : _images)
            out += (out.empty() ? "" : ",") + image;
        return "[" + out + "]";
    }

    std::string sounds() override
    {
        std::string out;
        for (const std::string& sound : _sounds)
            out += (out.empty() ? "" : ",") + sound;
        return "[" + out + "]";
    }

private:
    bool write(const QString& relative, const QByteArray& bytes)
    {
        const QString path = _folder.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        {
            notes << QString("%1 could not be written.").arg(relative);
            return false;
        }
        written << relative;
        return true;
    }

    static QByteArray wav(const PcmBuffer& pcm)
    {
        QByteArray data;
        data.resize(static_cast<qsizetype>(pcm.samples.size() * 2));
        auto* out = reinterpret_cast<int16_t*>(data.data());
        for (size_t i = 0; i < pcm.samples.size(); ++i)
            out[i] = static_cast<int16_t>(std::lround(std::clamp(pcm.samples[i], -1.0f, 1.0f) * 32767.0f));
        QByteArray file;
        QBuffer buffer(&file);
        buffer.open(QIODevice::WriteOnly);
        const auto u32 = [&buffer](uint32_t v) { buffer.write(reinterpret_cast<const char*>(&v), 4); };
        const auto u16 = [&buffer](uint16_t v) { buffer.write(reinterpret_cast<const char*>(&v), 2); };
        buffer.write("RIFF", 4);
        u32(static_cast<uint32_t>(36 + data.size()));
        buffer.write("WAVEfmt ", 8);
        u32(16);
        u16(1);
        u16(2);
        u32(PcmBuffer::kRate);
        u32(PcmBuffer::kRate * 4);
        u16(4);
        u16(16);
        buffer.write("data", 4);
        u32(static_cast<uint32_t>(data.size()));
        buffer.write(data);
        return file;
    }

    /// A picture, written once however often it is used.
    int imageIndex(const QImage& source, const std::string& name = std::string(), const std::string& linkage = std::string())
    {
        const QImage image = source.convertToFormat(QImage::Format_ARGB32);
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()));
        hash.addData(QByteArray::number(image.width()) + "x" + QByteArray::number(image.height()));
        const QByteArray key = hash.result();
        const auto found = _imageIndex.find(key);
        if (found != _imageIndex.end())
            return found->second;
        const int n = static_cast<int>(_images.size());
        // Opaque photographs as JPEG, the rest as PNG.
        bool opaque = !image.hasAlphaChannel();
        if (!opaque)
        {
            opaque = true;
            for (int y = 0; y < image.height() && opaque; ++y)
            {
                const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    if (qAlpha(line[x]) != 255)
                    {
                        opaque = false;
                        break;
                    }
            }
        }
        const bool jpeg = opaque && static_cast<qint64>(image.width()) * image.height() >= 256 * 256;
        const QString file = QString("%1/image%2.%3").arg(_assets).arg(n).arg(jpeg ? "jpg" : "png");
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, jpeg ? "JPG" : "PNG", jpeg ? 92 : -1);
        if (!write(file, bytes))
        {
            _imageIndex[key] = -1;
            return -1;
        }
        std::string entry = "{\"src\":" + fla::html5::quoted(file.toStdString()) + ",\"w\":" + std::to_string(image.width()) +
            ",\"h\":" + std::to_string(image.height());
        if (!name.empty())
            entry += ",\"name\":" + fla::html5::quoted(name);
        if (!linkage.empty())
            entry += ",\"linkage\":" + fla::html5::quoted(linkage);
        _images.push_back(entry + "}");
        _imageIndex[key] = n;
        return n;
    }

    /// A picture of a frame (see picture), the same pixels once; it waits
    /// to go on a sheet (packFrames).
    int frameIndex(const QImage& source)
    {
        const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()));
        hash.addData(QByteArray::number(image.width()) + "x" + QByteArray::number(image.height()));
        const QByteArray key = hash.result();
        const auto found = _frameIndex.find(key);
        if (found != _frameIndex.end())
            return found->second;
        const int n = static_cast<int>(_images.size());
        _images.push_back(std::string());
        _frames.emplace_back(n, image);
        _frameIndex[key] = n;
        return n;
    }

    /// \a image as the smaller of PNG and WebP (lossy, through Phoenix's
    /// ffmpeg, where it has one); \a suffix gets which.
    static QByteArray encoded(const QImage& image, QString& suffix)
    {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        suffix = "png";
        const QString ffmpeg = MovieExport::ffmpegPath();
        if (ffmpeg.isEmpty())
            return png;
        QTemporaryDir temporary;
        if (!temporary.isValid())
            return png;
        const QString in = temporary.filePath("in.png"), out = temporary.filePath("out.webp");
        QFile file(in);
        if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size())
            return png;
        file.close();
        Process process;
        process.start(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error", "-i", in, "-c:v", "libwebp", "-quality", "90",
                                  "-pix_fmt", image.hasAlphaChannel() ? "yuva420p" : "yuv420p", out});
        if (!process.waitForFinished(120000) || process.exitStatus() != Process::NormalExit || process.exitCode() != 0)
            return png;
        QFile webp(out);
        if (!webp.open(QIODevice::ReadOnly))
            return png;
        const QByteArray bytes = webp.readAll();
        if (bytes.isEmpty() || bytes.size() >= png.size())
            return png;
        suffix = "webp";
        return bytes;
    }

    /// The pictures of frames, packed onto sheets (one file holding many,
    /// each a part of it the content names) and written; one too big for a
    /// sheet is a file of its own.
    void packFrames()
    {
        if (_frames.empty())
            return;
        constexpr int kSheet = 2048;
        // Apart, so smoothing at a picture's edge takes nothing of the next.
        constexpr int kGap = 2;
        std::vector<size_t> order(_frames.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
            [this](size_t a, size_t b) { return _frames[a].second.height() > _frames[b].second.height(); });
        struct Sheet
        {
            std::vector<std::pair<int, QPoint>> placed;
            int x = 0, y = 0, shelf = 0, width = 0;
        };
        std::vector<Sheet> sheets;
        for (const size_t i : order)
        {
            const auto& [index, image] = _frames[i];
            if (image.width() > kSheet || image.height() > kSheet)
            {
                QString suffix;
                const QByteArray bytes = encoded(image, suffix);
                const QString file = QString("%1/frame%2.%3").arg(_assets).arg(index).arg(suffix);
                if (write(file, bytes))
                    _images[static_cast<size_t>(index)] = "{\"src\":" + fla::html5::quoted(file.toStdString()) +
                        ",\"w\":" + std::to_string(image.width()) + ",\"h\":" + std::to_string(image.height()) + "}";
                continue;
            }
            if (sheets.empty())
                sheets.emplace_back();
            Sheet* sheet = &sheets.back();
            if (sheet->x + image.width() > kSheet)
            {
                sheet->x = 0;
                sheet->y += sheet->shelf;
                sheet->shelf = 0;
            }
            if (sheet->y + image.height() > kSheet)
            {
                sheets.emplace_back();
                sheet = &sheets.back();
            }
            sheet->placed.emplace_back(static_cast<int>(i), QPoint(sheet->x, sheet->y));
            sheet->x += image.width() + kGap;
            sheet->width = std::max(sheet->width, sheet->x);
            sheet->shelf = std::max(sheet->shelf, image.height() + kGap);
        }
        for (const Sheet& sheet : sheets)
        {
            if (sheet.placed.empty())
                continue;
            QImage picture(std::max(1, sheet.width - kGap), std::max(1, sheet.y + sheet.shelf - kGap),
                QImage::Format_ARGB32_Premultiplied);
            picture.fill(Qt::transparent);
            {
                QPainter painter(&picture);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                for (const auto& [i, at] : sheet.placed)
                    painter.drawImage(at, _frames[static_cast<size_t>(i)].second);
            }
            const int n = static_cast<int>(_images.size());
            QString suffix;
            const QByteArray bytes = encoded(picture, suffix);
            const QString file = QString("%1/frames%2.%3").arg(_assets).arg(n).arg(suffix);
            if (!write(file, bytes))
                continue;
            _images.push_back("{\"src\":" + fla::html5::quoted(file.toStdString()) + ",\"w\":" +
                std::to_string(picture.width()) + ",\"h\":" + std::to_string(picture.height()) + ",\"sheet\":1}");
            for (const auto& [i, at] : sheet.placed)
            {
                const auto& [index, image] = _frames[static_cast<size_t>(i)];
                _images[static_cast<size_t>(index)] = "{\"in\":" + std::to_string(n) + ",\"r\":[" + std::to_string(at.x()) +
                    "," + std::to_string(at.y()) + "," + std::to_string(image.width()) + "," +
                    std::to_string(image.height()) + "],\"w\":" + std::to_string(image.width()) + ",\"h\":" +
                    std::to_string(image.height()) + "}";
            }
        }
        _frames.clear();
    }

    /// A brush as the runtime's fill JSON (README.md, "Shapes"); \a alpha
    /// multiplies a solid color's.
    std::string fillJson(const QBrush& brush, double alpha)
    {
        const QGradient* gradient = brush.gradient();
        if (gradient && (gradient->type() == QGradient::LinearGradient || gradient->type() == QGradient::RadialGradient))
        {
            // The gradient square (-819.2 to 819.2) onto the gradient, then
            // the brush's own transform.
            QTransform square;
            std::string extra;
            if (gradient->type() == QGradient::LinearGradient)
            {
                const auto* linear = static_cast<const QLinearGradient*>(gradient);
                const QPointF from = linear->start(), to = linear->finalStop();
                const QPointF d = to - from;
                square = QTransform(d.x() / 1638.4, d.y() / 1638.4, -d.y() / 1638.4, d.x() / 1638.4,
                    (from.x() + to.x()) / 2.0, (from.y() + to.y()) / 2.0);
            }
            else
            {
                const auto* radial = static_cast<const QRadialGradient*>(gradient);
                const QPointF center = radial->center();
                const QPointF toward = radial->focalPoint() - center;
                const double radius = std::max(radial->radius(), 1.0e-6);
                const double distance = std::hypot(toward.x(), toward.y());
                const double angle = distance > 1.0e-9 ? std::atan2(toward.y(), toward.x()) : 0.0;
                const double k = radius / 819.2;
                square = QTransform(std::cos(angle) * k, std::sin(angle) * k, -std::sin(angle) * k, std::cos(angle) * k,
                    center.x(), center.y());
                const double focal = std::clamp(distance / radius, 0.0, 0.99);
                if (focal > 0.0)
                    extra = ",\"fp\":" + number(focal, 4);
            }
            std::string stops;
            for (const QGradientStop& stop : gradient->stops())
                stops += std::string(stops.empty() ? "" : ",") + "[" + number(stop.first, 4) + "," +
                    std::to_string(stop.second.rgb() & 0xffffff) + "," + number(stop.second.alphaF() * alpha, 4) + "]";
            const char* spread = gradient->spread() == QGradient::ReflectSpread ? "reflect"
                : gradient->spread() == QGradient::RepeatSpread                 ? "repeat"
                                                                                : nullptr;
            return std::string("{\"g\":\"") + (gradient->type() == QGradient::LinearGradient ? "l" : "r") + "\",\"s\":[" +
                stops + "],\"m\":" + matrixJson(square * brush.transform()) + extra +
                (spread ? std::string(",\"sp\":\"") + spread + "\"" : std::string()) + "}";
        }
        if (gradient)
        {
            // A conical gradient: its first color.
            const QGradientStops stops = gradient->stops();
            const QColor color = stops.isEmpty() ? QColor(Qt::black) : stops.front().second;
            return "{\"c\":" + std::to_string(color.rgb() & 0xffffff) + ",\"a\":" + number(color.alphaF() * alpha, 4) + "}";
        }
        if (brush.style() == Qt::TexturePattern)
        {
            const QImage image = brush.textureImage().isNull() ? brush.texture().toImage() : brush.textureImage();
            const int index = imageIndex(image);
            if (index < 0)
                return std::string();
            return "{\"b\":" + std::to_string(index) + ",\"m\":" + matrixJson(brush.transform()) + ",\"r\":1}";
        }
        if (brush.style() == Qt::NoBrush)
            return std::string();
        const QColor color = brush.color();
        return "{\"c\":" + std::to_string(color.rgb() & 0xffffff) + ",\"a\":" + number(color.alphaF() * alpha, 4) + "}";
    }

    /// One recorded item as an op of the runtime's shape JSON, its reach
    /// added to \a box.
    std::string opJson(const RecordedItem& item, QRectF& box)
    {
        const bool solid = !item.brush.gradient() && item.brush.style() != Qt::TexturePattern;
        std::string extra;
        if (item.opacity < 1.0 && !(solid && item.kind != RecordedItem::Kind::Image))
            extra += ",\"o\":" + number(item.opacity, 4);
        if (const char* blend = blendOf(item.composition))
            extra += std::string(",\"bm\":\"") + blend + "\"";
        if (item.clipped)
            extra += ",\"clip\":" + fla::html5::quoted(pathData(item.clip));
        const double alpha = solid ? item.opacity : 1.0;
        const auto grow = [&box](const QRectF& reach) {
            if (reach.isValid())
                box = box.isValid() ? box.united(reach) : reach;
        };
        switch (item.kind)
        {
        case RecordedItem::Kind::Fill:
        {
            if (item.path.isEmpty())
                return std::string();
            const std::string fill = fillJson(item.brush, alpha);
            if (fill.empty())
                return std::string();
            const QRectF reach = item.path.boundingRect();
            grow(reach);
            return "{\"p\":" + fla::html5::quoted(pathData(item.path)) + ",\"f\":" + fill +
                (item.path.fillRule() == Qt::OddEvenFill ? ",\"e\":1" : "") + ",\"b\":" + boxJson(reach) + extra + "}";
        }
        case RecordedItem::Kind::Stroke:
        {
            const QPen& pen = item.pen;
            if (pen.style() != Qt::SolidLine)
            {
                // Dashes and the rest: the area the stroke covers.
                QPainterPathStroker stroker(pen);
                stroker.setWidth(pen.widthF() > 0.0 ? pen.widthF() : 1.0);
                RecordedItem area = item;
                area.kind = RecordedItem::Kind::Fill;
                area.path = stroker.createStroke(item.path);
                return opJson(area, box);
            }
            const std::string fill = fillJson(item.brush, alpha);
            if (fill.empty())
                return std::string();
            const char* cap = pen.capStyle() == Qt::FlatCap ? "none" : pen.capStyle() == Qt::SquareCap ? "square" : "round";
            const char* join = pen.joinStyle() == Qt::BevelJoin ? "bevel" : pen.joinStyle() == Qt::RoundJoin ? "round" : "miter";
            const double width = pen.widthF();
            const QRectF reach = item.path.boundingRect().adjusted(-width / 2, -width / 2, width / 2, width / 2);
            grow(reach);
            std::string line = "{\"w\":" + number(width, 3) + ",\"f\":" + fill + ",\"cap\":\"" + cap + "\",\"join\":\"" +
                join + "\"";
            if (pen.joinStyle() != Qt::RoundJoin)
                line += ",\"ml\":" + number(pen.miterLimit(), 2);
            if (pen.isCosmetic())
                line += ",\"sm\":\"none\"";
            return "{\"p\":" + fla::html5::quoted(pathData(item.path)) + ",\"s\":" + line + "},\"b\":" + boxJson(reach) + extra + "}";
        }
        case RecordedItem::Kind::Image:
        {
            if (item.image.isNull())
                return std::string();
            const int index = imageIndex(item.image);
            if (index < 0)
                return std::string();
            const QRectF reach = item.path.boundingRect();
            grow(reach);
            std::string op = "{\"i\":" + std::to_string(index) + ",\"m\":" + matrixJson(item.imageTransform) + ",\"b\":" +
                boxJson(reach);
            if (item.opacity < 1.0)
                op += ",\"o\":" + number(item.opacity, 4);
            if (const char* blend = blendOf(item.composition))
                op += std::string(",\"bm\":\"") + blend + "\"";
            if (item.clipped)
                op += ",\"clip\":" + fla::html5::quoted(pathData(item.clip));
            return op + "}";
        }
        case RecordedItem::Kind::Text:
        {
            if (item.text.trimmed().isEmpty())
                return std::string();
            const std::string fill = fillJson(item.brush, alpha);
            if (fill.empty())
                return std::string();
            const QFont& font = item.font;
            const double size = font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * 96.0 / 72.0;
            std::string css;
            if (font.italic())
                css += "italic ";
            if (font.weight() >= QFont::DemiBold)
                css += "bold ";
            // The family the font resolved to here, then the one asked for.
            const QString resolved = QFontInfo(font).family();
            css += number(size, 2) + "px \"" + resolved.toStdString() + "\"";
            if (font.family() != resolved)
                css += ", \"" + font.family().toStdString() + "\"";
            css += ", sans-serif";
            const QRectF reach = item.path.boundingRect();
            grow(reach);
            std::string op = "{\"t\":" + fla::html5::quoted(item.text.toStdString()) + ",\"font\":" + fla::html5::quoted(css) + ",\"x\":" +
                number(item.at.x(), 2) + ",\"y\":" + number(item.at.y(), 2) + ",\"f\":" + fill;
            if (!item.transform.isIdentity())
                op += ",\"m\":" + matrixJson(item.transform);
            if (font.letterSpacingType() == QFont::AbsoluteSpacing && font.letterSpacing() != 0.0)
                op += ",\"ls\":" + number(font.letterSpacing(), 2);
            return op + ",\"b\":" + boxJson(reach) + extra + "}";
        }
        }
        return std::string();
    }

    PhoenixView& _view;
    TimelineAudio* _audio;
    const fla::Document& _document;
    QDir _folder;
    QString _assets;
    std::vector<std::string> _images;
    std::map<QByteArray, int> _imageIndex;
    /// Pictures of frames waiting to go on sheets, by their index.
    std::vector<std::pair<int, QImage>> _frames;
    std::map<QByteArray, int> _frameIndex;
    std::vector<std::string> _sounds;
    std::map<std::string, int> _soundIndex;
    std::map<std::string, std::string> _soundLinkage;
};

QString pageHtml(const QString& title, const QString& name, const QColor& background, bool fit, bool javaScript,
    bool actionScript, bool actionScript2, int width, int height)
{
    const QString style = fit
        ? QString("html, body { margin: 0; height: 100%; overflow: hidden; background: %1; }\n"
                  "#phoenix-stage { position: relative; width: 100vw; height: 100vh; display: flex; align-items: center; "
                  "justify-content: center; }\n").arg(background.name())
        : QString("body { margin: 0; background: %1; }\n"
                  "#phoenix-stage { position: relative; width: %2px; height: %3px; }\n")
              .arg(background.name()).arg(width).arg(height);
    QString scripts = "<script src=\"phoenix.js\"></script>\n";
    if (javaScript)
        scripts += "<script src=\"phoenix-createjs.js\"></script>\n";
    if (actionScript2)
        scripts += "<script src=\"phoenix-as2.js\"></script>\n";
    scripts += QString("<script src=\"%1.content.js\"></script>\n").arg(name.toHtmlEscaped());
    // The movie's ActionScript as JavaScript, which the runtime loads
    // before the movie starts (see the runtime's README).
    const QString start = actionScript
        ? QString("// %1.as.js is the movie's ActionScript as JavaScript: the runtime loads it before the movie\n"
                  "// starts (see the runtime's README).\n"
                  "phoenix.start({container: \"phoenix-stage\", scale: \"%2\", actionscript: \"%1.as.js\"});\n")
              .arg(name, fit ? "fit" : "none")
        : QString("phoenix.start({container: \"phoenix-stage\", scale: \"%1\"});\n").arg(fit ? "fit" : "none");
    return QString("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                   "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>%1</title>\n"
                   "<style>\n%2</style>\n%3</head>\n<body>\n<div id=\"phoenix-stage\"></div>\n<script>\n%4"
                   "</script>\n</body>\n</html>\n")
        .arg(title.toHtmlEscaped(), style, scripts, start);
}

} // namespace

QByteArray runtime()
{
    QByteArray out = "// Phoenix HTML5 runtime: the flash.* display list, events and timelines over a canvas.\n";
    out += "\n// as3.js (libas3): what ActionScript translated to JavaScript runs on.\n";
    out += QByteArray::fromStdString(as3::js::runtime());
    for (const char* file : kRuntimeFiles)
    {
        out += "\n// " + QByteArray(file) + ".js\n";
        out += resource(QString(file) + ".js");
    }
    return out;
}

QByteArray createJs()
{
    return resource("createjs.js");
}

QByteArray actionScript(const fla::Document& document, const QString& source, const QString& name, QByteArray& map,
    QStringList& notes)
{
    // The classes as Test Movie and SWF publishing find them: the class
    // path, then Phoenix's own (the fl.* components, translated too).
    const fla::play::ClassPath classPath = movieClassPath(document, source);
    fla::swfmovie::ActionScript3Options options;
    options.findClass = [classPath](const std::string& qualified) -> std::optional<std::string> {
        const std::optional<fla::play::ClassFile> found = classPath.find(qualified);
        return found ? std::optional<std::string>(found->source) : std::nullopt;
    };
    const QString folder = source.isEmpty() ? QString() : QFileInfo(source).absolutePath();
    options.readFile = [folder](const std::string& path) -> std::optional<std::string> {
        QFile file(QDir(folder).filePath(QString::fromStdString(path)));
        if (folder.isEmpty() || !file.open(QIODevice::ReadOnly))
            return std::nullopt;
        return file.readAll().toStdString();
    };
    options.documentName = source.isEmpty() ? std::string("Untitled") : QFileInfo(source).completeBaseName().toStdString();
    const fla::swfmovie::JavaScript3 translated = fla::swfmovie::translateActionScript3(document, options);
    for (const std::string& note : translated.notes)
        notes << QString::fromStdString(note);
    if (translated.bundle.empty())
        return QByteArray();
    std::string bundle = translated.bundle;
    // The bundle's own source map comment names libas3's file; the page's
    // is beside it.
    const size_t comment = bundle.rfind("//# sourceMappingURL=");
    if (comment != std::string::npos)
        bundle.erase(comment);
    if (!bundle.empty() && bundle.back() != '\n')
        bundle += '\n';
    // What binds the classes to the movie: the main timeline's class, and
    // each symbol's (by its library name; linkage classes the runtime binds
    // by itself).
    std::string link = "\n// The movie's classes: its main timeline's, and its symbols'.\n"
                       "phoenix.actionscript(function (movie) {\n";
    if (!translated.mainClass.empty())
        link += "    movie.documentClass(as3.use(" + fla::html5::quoted(translated.mainClass) + "));\n";
    for (const auto& [symbol, cls] : translated.classes)
        if (symbol)
            link += "    movie.linkClass(" + fla::html5::quoted(symbol->name) + ", as3.use(" + fla::html5::quoted(cls) +
                "));\n";
    link += "});\n";
    map = QByteArray::fromStdString(translated.sourceMap);
    return QByteArray::fromStdString(bundle + link) +
        (map.isEmpty() ? QByteArray() : "//# sourceMappingURL=" + name.toUtf8() + ".as.js.map\n");
}

QByteArray actionScript2Runtime()
{
    return "// avm1.js (libas3): ActionScript 1 and 2's semantics for translated scripts.\n" +
        QByteArray::fromStdString(fla::html5as2::avm1Runtime()) + "\n// as2.js\n" + resource("as2.js") +
        "\n// as2components.js\n" + resource("as2components.js");
}

bool run(const Settings& settings, PhoenixView& view, TimelineAudio* audio, const fla::Document& document,
    QStringList& written, QStringList& notes, QString& error)
{
    const QFileInfo page(settings.page);
    const QDir folder = page.absoluteDir();
    if (!QDir().mkpath(folder.absolutePath()))
    {
        error = QString("Could not make %1.").arg(QDir::toNativeSeparators(folder.absolutePath()));
        return false;
    }
    const QString name = page.completeBaseName();
    const bool javaScript = fla::canvas::isCanvasDocument(document);
    ViewArt art(view, audio, document, folder, name + "_assets");
    art.linkedSounds();
    fla::html5::Options options;
    options.javaScript = javaScript;
    options.loop = settings.loop;
    // ActionScript 1 and 2 are translated to JavaScript (html5as2), the
    // scripts gathered as the content is made; ActionScript 3 is translated
    // too (an HTML5 Canvas document's scripts are JavaScript already, in
    // its content).
    fla::html5::ActionScript2 actionScript2;
    const bool as2 = !javaScript && fla::html5as2::usesActionScript2(document);
    if (as2)
        options.actionScript2 = &actionScript2;
    const int version = fla::play::actionScriptVersion(document);
    const bool translate = !javaScript && !as2 && (version == 0 || version == 3);
    options.actionScriptTranslated = translate;
    std::vector<std::string> found;
    const std::string content = fla::html5::content(document, art, options, &found);
    for (const std::string& note : found)
        notes << QString::fromStdString(note);
    notes << art.notes;
    written << art.written;

    const auto save = [&](const QString& file, const QByteArray& bytes) {
        QFile out(folder.filePath(file));
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
        {
            error = QString("Could not write %1.").arg(QDir::toNativeSeparators(folder.filePath(file)));
            return false;
        }
        written << file;
        return true;
    };
    const QByteArray runtimeScript = runtime();
    if (runtimeScript.isEmpty())
    {
        error = "This Phoenix has no HTML5 runtime built in.";
        return false;
    }
    if (!save("phoenix.js", runtimeScript))
        return false;
    if (javaScript && !save("phoenix-createjs.js", createJs()))
        return false;
    if (!save(name + ".content.js", "phoenix.content(" + QByteArray::fromStdString(content) + ");\n"))
        return false;
    bool scripted = false;
    if (translate)
    {
        QByteArray map;
        const QByteArray script = actionScript(document, settings.source, name, map, notes);
        if (!script.isEmpty())
        {
            if (!save(name + ".as.js", script))
                return false;
            if (!map.isEmpty() && !save(name + ".as.js.map", map))
                return false;
            scripted = true;
        }
    }
    if (as2)
    {
        const fla::play::ClassPath classPath = movieClassPath(document, settings.source);
        std::vector<std::string> left;
        const fla::html5as2::Result translated = fla::html5as2::translate(document, actionScript2,
            [&classPath](const std::string& qualified) -> std::optional<std::string> {
                const std::optional<fla::play::ClassFile> found = classPath.find(qualified);
                return found ? std::optional<std::string>(found->source) : std::nullopt;
            },
            &left);
        for (const std::string& note : left)
            notes << QString::fromStdString(note);
        if (!save("phoenix-as2.js", actionScript2Runtime()) ||
            !save(name + ".as.js", QByteArray::fromStdString(translated.code)))
            return false;
    }
    const QColor background(document.backgroundColor[0], document.backgroundColor[1], document.backgroundColor[2]);
    const QString html = pageHtml(name, name, background, settings.fit, javaScript, scripted || as2, as2,
        std::max(1, document.width),
        std::max(1, document.height));
    return save(page.fileName(), html.toUtf8());
}

} // namespace Html5Publish
