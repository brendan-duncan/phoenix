#include "library_media.h"

#include "library_commands.h"

#include "../data/bitmap.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>

namespace fla {

namespace media {

namespace {

/// Escapes text for an XML attribute.
std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

bool dataNameUsed(const Document& document, const std::string& name)
{
    for (const Resource* resource : document.resources)
    {
        if (resource && resource->resourceType() == Resource::Type::Bitmap &&
            static_cast<const Bitmap*>(resource)->bitmapDataHRef == name)
            return true;
    }
    const std::string path = "bin/" + name;
    for (const auto& file : document.extraFiles)
    {
        if (file.first == path)
            return true;
    }
    return false;
}

} // namespace

std::string freeName(const Document& document, const std::string& wanted)
{
    if (library::nameIsFree(document, wanted))
        return wanted;
    const size_t dot = wanted.find_last_of('.');
    const size_t slash = wanted.find_last_of('/');
    const bool extension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string stem = extension ? wanted.substr(0, dot) : wanted;
    const std::string suffix = extension ? wanted.substr(dot) : std::string();
    for (int n = 2;; ++n)
    {
        const std::string name = stem + " " + std::to_string(n) + suffix;
        if (library::nameIsFree(document, name))
            return name;
    }
}

std::string newItemId()
{
    static std::atomic<uint32_t> counter{1};
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%08x-%08x", static_cast<unsigned>(std::time(nullptr)),
        static_cast<unsigned>(counter++));
    return buffer;
}

std::string newDataName(const Document& document)
{
    const long long now = static_cast<long long>(std::time(nullptr));
    for (int n = 1;; ++n)
    {
        const std::string name = "M " + std::to_string(n) + " " + std::to_string(now) + ".dat";
        if (!dataNameUsed(document, name))
            return name;
    }
}

Bitmap* newBitmap(const Document& document, const std::string& name, const std::string& sourcePath,
    int width, int height, std::vector<uint8_t> data, bool jpeg)
{
    Bitmap* bitmap = new Bitmap(nullptr);
    bitmap->name = freeName(document, name);
    bitmap->itemId = newItemId();
    bitmap->href = bitmap->name;
    bitmap->bitmapDataHRef = newDataName(document);
    bitmap->sourceExternalFilepath = sourcePath;
    bitmap->sourceLastImported = static_cast<uint32_t>(std::time(nullptr));
    bitmap->allowSmoothing = true;
    bitmap->quality = 50;
    bitmap->frameRight = width * 20;
    bitmap->frameBottom = height * 20;
    bitmap->datData = std::move(data);
    // How Animate marks the two encodings.
    if (jpeg)
        bitmap->extraAttributes.emplace_back("isJPEG", "true");
    else
        bitmap->extraAttributes.emplace_back("originalCompressionType", "lossless");
    return bitmap;
}

} // namespace media

AddBitmapCommand::AddBitmapCommand(Document* document, Bitmap* bitmap, const std::string& name)
    : _document(document)
    , _bitmap(bitmap)
    , _name(name)
{
}

AddBitmapCommand::~AddBitmapCommand()
{
    if (_owned)
        delete _bitmap;
}

void AddBitmapCommand::redo()
{
    if (!_document || !_bitmap)
        return;
    _bitmap->parent = _document;
    _document->resources.push_back(_bitmap);
    _owned = false;
}

void AddBitmapCommand::undo()
{
    if (!_document || !_bitmap)
        return;
    std::vector<Resource*>& resources = _document->resources;
    resources.erase(std::remove(resources.begin(), resources.end(), _bitmap), resources.end());
    _owned = true;
}

AddSoundCommand::AddSoundCommand(Document* document, const media::Sound& sound, const std::string& name)
    : _document(document)
    , _name(name)
{
    if (!document)
        return;
    const int rate = sound.rate;
    const char* khz = rate <= 5512 ? "5kHz" : rate <= 11025 ? "11kHz" : rate <= 22050 ? "22kHz" : "44kHz";
    _info.name = media::freeName(*document, sound.name);
    _info.format = std::string(khz) + " 16bit " + (sound.channels == 2 ? "Stereo" : "Mono");
    const std::string data = media::newDataName(*document);
    _info.dataPath = "bin/" + data;
    _data.resize(sound.samples.size() * 2);
    for (size_t i = 0; i < sound.samples.size(); ++i)
    {
        const uint16_t sample = static_cast<uint16_t>(sound.samples[i]);
        _data[i * 2] = static_cast<uint8_t>(sample);
        _data[i * 2 + 1] = static_cast<uint8_t>(sample >> 8);
    }
    const size_t frames = sound.samples.size() / static_cast<size_t>(std::max(sound.channels, 1));
    _xml = "<DOMSoundItem name=\"" + media::escaped(_info.name) + "\" itemID=\"" + media::newItemId() +
        "\" sourceExternalFilepath=\"" + media::escaped(sound.sourcePath) + "\" sourceLastImported=\"" +
        std::to_string(static_cast<long long>(std::time(nullptr))) + "\" href=\"" + media::escaped(_info.name) +
        "\" soundDataHRef=\"" + media::escaped(data) + "\" format=\"" + _info.format + "\" sampleCount=\"" +
        std::to_string(frames) + "\" dataLength=\"" + std::to_string(_data.size()) + "\"/>";
}

void AddSoundCommand::redo()
{
    if (!_document || _xml.empty())
        return;
    _document->extraMedia.push_back(_xml);
    _document->sounds.push_back(_info);
    _document->extraFiles.emplace_back(_info.dataPath, _data);
}

void AddSoundCommand::undo()
{
    if (!_document || _xml.empty())
        return;
    auto& media = _document->extraMedia;
    media.erase(std::remove(media.begin(), media.end(), _xml), media.end());
    auto& sounds = _document->sounds;
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
        [this](const Document::SoundInfo& sound) { return sound.name == _info.name; }), sounds.end());
    auto& files = _document->extraFiles;
    files.erase(std::remove_if(files.begin(), files.end(),
        [this](const std::pair<std::string, std::vector<uint8_t>>& file) { return file.first == _info.dataPath; }),
        files.end());
}

} // namespace fla
