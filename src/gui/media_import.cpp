#include "media_import.h"

#include "../data/bitmap.h"
#include "../data/bitmap_decoder.h"
#include "../data/document.h"
#include "../third_party/miniaudio/miniaudio.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <map>

namespace MediaImport {

namespace {

const QStringList kImageSuffixes = {"png", "jpg", "jpeg", "gif", "bmp"};
const QStringList kSoundSuffixes = {"wav", "aif", "aiff", "mp3", "flac", "ogg"};

/// The pixels of an image as the lossless encoding takes them.
std::vector<uint32_t> pixelsOf(const QImage& image)
{
    const QImage premultiplied = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    std::vector<uint32_t> pixels(static_cast<size_t>(premultiplied.width()) * premultiplied.height());
    for (int y = 0; y < premultiplied.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(premultiplied.constScanLine(y));
        std::copy(line, line + premultiplied.width(), pixels.begin() + static_cast<size_t>(y) * premultiplied.width());
    }
    return pixels;
}

} // namespace

bool isImage(const QString& path)
{
    return kImageSuffixes.contains(QFileInfo(path).suffix().toLower());
}

bool isSound(const QString& path)
{
    return kSoundSuffixes.contains(QFileInfo(path).suffix().toLower());
}

bool isArtwork(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "svg" || suffix == "ai" || suffix == "pdf" || suffix == "psd";
}

fla::Bitmap* loadImage(const fla::Document& document, const QString& path, QString& error)
{
    QImage image(path);
    if (image.isNull())
    {
        error = QString("%1 could not be read as an image.").arg(QFileInfo(path).fileName());
        return nullptr;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const std::string name = QFileInfo(path).fileName().toStdString();
    if (suffix == "jpg" || suffix == "jpeg")
    {
        // A JPEG goes in as it is, as Animate keeps one.
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
        {
            const QByteArray bytes = file.readAll();
            return fla::media::newBitmap(document, name, path.toStdString(), image.width(), image.height(),
                std::vector<uint8_t>(bytes.begin(), bytes.end()), true);
        }
    }
    if (image.width() > 0xffff / 4 || image.height() > 0xffff)
    {
        error = QString("%1 is too large to import.").arg(QFileInfo(path).fileName());
        return nullptr;
    }
    return fla::media::newBitmap(document, name, path.toStdString(), image.width(), image.height(),
        fla::encodeLosslessBitmap(image.width(), image.height(), pixelsOf(image)), false);
}

fla::Bitmap* bitmapFromImage(const fla::Document& document, const QImage& image, const QString& name)
{
    if (image.isNull() || image.width() > 0xffff / 4 || image.height() > 0xffff)
        return nullptr;
    return fla::media::newBitmap(document, name.toStdString(), std::string(), image.width(), image.height(),
        fla::encodeLosslessBitmap(image.width(), image.height(), pixelsOf(image)), false);
}

std::vector<uint8_t> losslessData(const QImage& image)
{
    return fla::encodeLosslessBitmap(image.width(), image.height(), pixelsOf(image));
}

bool loadSound(const QString& path, fla::media::Sound& sound, QString& error)
{
    // Read once as it is to learn its channels, then as 16-bit at 44.1 kHz
    // in no more than two of them.
    ma_decoder probe;
    const std::wstring wide = path.toStdWString();
    if (ma_decoder_init_file_w(wide.c_str(), nullptr, &probe) != MA_SUCCESS)
    {
        error = QString("%1 could not be read as a sound.").arg(QFileInfo(path).fileName());
        return false;
    }
    const ma_uint32 channels = std::min<ma_uint32>(std::max<ma_uint32>(probe.outputChannels, 1), 2);
    ma_decoder_uninit(&probe);

    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, channels, 44100);
    ma_decoder decoder;
    if (ma_decoder_init_file_w(wide.c_str(), &config, &decoder) != MA_SUCCESS)
    {
        error = QString("%1 could not be read as a sound.").arg(QFileInfo(path).fileName());
        return false;
    }
    sound.name = QFileInfo(path).fileName().toStdString();
    sound.sourcePath = path.toStdString();
    sound.rate = 44100;
    sound.channels = static_cast<int>(channels);
    sound.samples.clear();
    std::vector<int16_t> chunk(4096 * channels);
    for (;;)
    {
        ma_uint64 read = 0;
        if (ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read) != MA_SUCCESS && read == 0)
            break;
        if (read == 0)
            break;
        sound.samples.insert(sound.samples.end(), chunk.begin(), chunk.begin() + static_cast<long long>(read * channels));
    }
    ma_decoder_uninit(&decoder);
    if (sound.samples.empty())
    {
        error = QString("%1 holds no sound.").arg(QFileInfo(path).fileName());
        return false;
    }
    return true;
}

QStringList sequenceOf(const QString& path)
{
    // A name ending in digits before its extension: the files beside it that
    // differ only in those digits, in number order.
    const QFileInfo info(path);
    static const QRegularExpression numbered("^(.*?)(\\d+)$");
    const QRegularExpressionMatch match = numbered.match(info.completeBaseName());
    if (!match.hasMatch())
        return {};
    const QString stem = match.captured(1);
    const QString suffix = info.suffix();
    std::map<long long, QString> found;
    const QDir folder = info.dir();
    for (const QFileInfo& other : folder.entryInfoList(QDir::Files))
    {
        if (other.suffix().compare(suffix, Qt::CaseInsensitive) != 0)
            continue;
        const QRegularExpressionMatch each = numbered.match(other.completeBaseName());
        if (each.hasMatch() && each.captured(1) == stem)
            found[each.captured(2).toLongLong()] = other.absoluteFilePath();
    }
    if (found.size() < 2)
        return {};
    QStringList files;
    for (const auto& entry : found)
        files << entry.second;
    return files;
}

} // namespace MediaImport
