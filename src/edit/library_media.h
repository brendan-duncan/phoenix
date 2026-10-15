#pragma once

#include "command.h"

#include "../data/document.h"
#include "../data/lazy_bytes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Bitmap;

/// Bringing bitmaps and sounds into a document's library as FLA files keep
/// them: a bitmap's pixels in a bin/*.dat of the FLA's own encoding (a
/// lossless one, or the JPEG as it was), a sound's samples as raw PCM in
/// one. Nothing here decodes image or sound files; the caller hands over
/// the pixels or the samples.
namespace media {

/// \a wanted, or "name 2.png", "name 3.png"... whichever the library does
/// not have yet.
std::string freeName(const Document& document, const std::string& wanted);

/// A new item id in the FLA's form, eight hex digits, a dash, eight more.
std::string newItemId();

/// A name for a new data file under bin, "M <n> <seconds>.dat", that no
/// item of the document uses yet.
std::string newDataName(const Document& document);

/// A library bitmap for pixels already encoded as \a data: the lossless
/// .dat (encodeLosslessBitmap) or, with \a jpeg, the JPEG file itself. The
/// caller owns it until an AddBitmapCommand takes it.
Bitmap* newBitmap(const Document& document, const std::string& name, const std::string& sourcePath,
    int width, int height, std::vector<uint8_t> data, bool jpeg);

/// A sound for the library: 16-bit samples, interleaved when stereo, at
/// one of the FLA's rates (5512, 11025, 22050 or 44100).
struct Sound
{
    std::string name;
    std::string sourcePath;
    int rate = 44100;
    int channels = 1;
    std::vector<int16_t> samples;
};

/// Lets go of the bitmaps' and sounds' bytes that were read from the
/// document's file, as for a document waiting in a tab; they are read again
/// when next shown. The number of bytes let go.
size_t releaseLoadedMedia(const Document& document);

} // namespace media

/// Adds a bitmap to the library.
class AddBitmapCommand : public Command
{
public:
    /// Takes ownership of \a bitmap.
    AddBitmapCommand(Document* document, Bitmap* bitmap, const std::string& name);
    ~AddBitmapCommand() override;
    AddBitmapCommand(const AddBitmapCommand&) = delete;
    AddBitmapCommand& operator=(const AddBitmapCommand&) = delete;
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    Document* _document;
    Bitmap* _bitmap;
    std::string _name;
    bool _owned = true;
};

/// Gives a library bitmap new pixels, as a bitmap brush paints into its
/// canvas: \a data in the lossless encoding, \a width by \a height.
class SetBitmapPixelsCommand : public Command
{
public:
    SetBitmapPixelsCommand(Bitmap* bitmap, std::vector<uint8_t> data, int width, int height, const std::string& name);
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    struct Pixels
    {
        LazyBytes data;
        LazyBytes image;
        int frameRight = 0;
        int frameBottom = 0;
        bool jpeg = false;
    };
    void apply(const Pixels& pixels);

    Bitmap* _bitmap;
    Pixels _before;
    Pixels _after;
    std::string _name;
};

/// Adds a sound to the library: its item, as the FLA stores a
/// DOMSoundItem, and its data under bin.
class AddSoundCommand : public Command
{
public:
    AddSoundCommand(Document* document, const media::Sound& sound, const std::string& name);
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

    /// The library name the sound went in under.
    const std::string& soundName() const { return _info.name; }

private:
    Document* _document;
    std::string _name;
    std::string _xml;
    Document::SoundInfo _info;
    std::vector<uint8_t> _data;
};

} // namespace fla
