#pragma once

#include "../edit/library_media.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <cstdint>

namespace fla {
class Bitmap;
class Document;
}

/// Reading image and sound files for File > Import, a drop from the desktop
/// or a paste, into what the library keeps (see edit/library_media.h).
namespace MediaImport {

/// Whether a file is one Import takes, by its extension.
bool isImage(const QString& path);
bool isSound(const QString& path);
/// Vector artwork (SVG, Illustrator, PDF) or a layered Photoshop document.
bool isArtwork(const QString& path);

/// A library bitmap for an image file: a JPEG kept as it is, anything else
/// encoded losslessly. Null, with \a error saying why, when it cannot be
/// read. The caller owns it.
fla::Bitmap* loadImage(const fla::Document& document, const QString& path, QString& error);

/// A library bitmap for pixels held in memory, a pasted picture, named
/// \a name.
fla::Bitmap* bitmapFromImage(const fla::Document& document, const QImage& image, const QString& name);

/// An image's pixels in Animate's lossless bitmap encoding.
std::vector<uint8_t> losslessData(const QImage& image);

/// Decodes a sound file (WAV, AIFF, MP3, FLAC, Ogg) to 16-bit samples at
/// 44.1 kHz, mono or stereo as it was. False, with \a error, when it cannot.
bool loadSound(const QString& path, fla::media::Sound& sound, QString& error);

/// The numbered files of a sequence \a path belongs to ("walk0001.png",
/// "walk0002.png"...), in order, when there are more than one; otherwise
/// empty.
QStringList sequenceOf(const QString& path);

} // namespace MediaImport
