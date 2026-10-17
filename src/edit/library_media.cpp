#include "library_media.h"

#include "element_commands.h"
#include "frame_sound.h"
#include "library_commands.h"
#include "property_command.h"
#include "../data/bitmap.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/raw_element.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

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
    // How the FLA marks the two encodings.
    if (jpeg)
        bitmap->extraAttributes.emplace_back("isJPEG", "true");
    else
        bitmap->extraAttributes.emplace_back("originalCompressionType", "lossless");
    return bitmap;
}

size_t releaseLoadedMedia(const Document& document)
{
    size_t released = 0;
    const auto release = [&released](const LazyBytes& bytes) {
        if (!bytes.loaded())
            return;
        const size_t size = bytes.size();
        if (bytes.unload())
            released += size;
    };
    for (const Resource* resource : document.resources)
    {
        if (!resource || resource->resourceType() != Resource::Type::Bitmap)
            continue;
        const Bitmap* bitmap = static_cast<const Bitmap*>(resource);
        release(bitmap->imageData);
        release(bitmap->datData);
    }
    for (const auto& file : document.extraFiles)
        release(file.second);
    return released;
}

std::vector<CommandPtr> removeUses(Document& document, const std::string& item)
{
    std::vector<CommandPtr> commands;
    const auto timeline = [&](Timeline* timeline) {
        if (!timeline)
            return;
        for (Layer* layer : timeline->layers)
        {
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                // The keyframe keeps playing nothing, its other settings
                // gone with the sound.
                if (frame_sound::read(*frame).name == item)
                {
                    auto attributes = frame->extraAttributes;
                    auto children = frame->extraChildren;
                    frame_sound::write(frame_sound::Settings(), 0, attributes, children);
                    commands.push_back(CommandPtr(new SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                        frame->extraAttributes, frame->extraAttributes, attributes, "Delete Sound")));
                    commands.push_back(CommandPtr(new SetPropertyCommand<std::vector<std::string>>(
                        frame->extraChildren, frame->extraChildren, children, "Delete Sound")));
                }
                for (Element* element : frame->elements)
                {
                    auto* video = dynamic_cast<RawElement*>(element);
                    if (video && video->videoItem == item)
                        commands.push_back(CommandPtr(new RemoveElementCommand(frame, video, "Delete Video")));
                }
            }
        }
    };
    for (Timeline* t : document.timelines)
        timeline(t);
    if (document.symbolList)
    {
        for (Symbol* symbol : document.symbolList->symbols)
        {
            if (symbol)
            {
                for (Timeline* t : symbol->timelines)
                    timeline(t);
            }
        }
    }
    return commands;
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



SetBitmapPixelsCommand::SetBitmapPixelsCommand(Bitmap* bitmap, std::vector<uint8_t> data, int width, int height,
    const std::string& name)
    : _bitmap(bitmap)
    , _name(name)
{
    if (_bitmap)
    {
        _before.data = _bitmap->datData;
        _before.image = _bitmap->imageData;
        _before.frameRight = _bitmap->frameRight;
        _before.frameBottom = _bitmap->frameBottom;
        _before.jpeg = _bitmap->isJPEG;
    }
    _after.data = LazyBytes(std::move(data));
    _after.frameRight = width * 20;
    _after.frameBottom = height * 20;
}

void SetBitmapPixelsCommand::apply(const Pixels& pixels)
{
    if (!_bitmap)
        return;
    _bitmap->datData = pixels.data;
    _bitmap->imageData = pixels.image;
    _bitmap->frameRight = pixels.frameRight;
    _bitmap->frameBottom = pixels.frameBottom;
    _bitmap->isJPEG = pixels.jpeg;
}

void SetBitmapPixelsCommand::redo()
{
    apply(_after);
}

void SetBitmapPixelsCommand::undo()
{
    apply(_before);
}

RemoveMediaCommand::RemoveMediaCommand(Document* document, const std::string& itemName, const std::string& name)
    : _document(document)
    , _itemName(itemName)
    , _name(name)
{
}

namespace {

/// The value of \a key on the opening tag of \a xml, as written (escaped).
std::string tagAttribute(const std::string& xml, const std::string& key)
{
    const size_t tagEnd = xml.find('>');
    const size_t at = xml.find(" " + key + "=\"");
    if (at == std::string::npos || at > tagEnd)
        return {};
    const size_t start = at + key.size() + 3;
    const size_t end = xml.find('"', start);
    return end == std::string::npos ? std::string() : xml.substr(start, end - start);
}

} // namespace

void RemoveMediaCommand::redo()
{
    if (!_document)
        return;
    _taken = Taken();
    // Its item, a DOMSoundItem or DOMVideoItem kept as text.
    auto& media = _document->extraMedia;
    std::string dataPath;
    for (size_t i = 0; i < media.size(); ++i)
    {
        const std::string& xml = media[i];
        if ((xml.rfind("<DOMSoundItem", 0) != 0 && xml.rfind("<DOMVideoItem", 0) != 0) ||
            tagAttribute(xml, "name") != media::escaped(_itemName))
            continue;
        const std::string video = tagAttribute(xml, "videoDataHRef");
        if (!video.empty())
            dataPath = "bin/" + video;
        _taken.xmlTaken = true;
        _taken.xmlIndex = i;
        _taken.xml = xml;
        media.erase(media.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    auto& sounds = _document->sounds;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        if (sounds[i].name != _itemName)
            continue;
        dataPath = sounds[i].dataPath;
        _taken.soundTaken = true;
        _taken.soundIndex = i;
        _taken.sound = sounds[i];
        sounds.erase(sounds.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    auto& videos = _document->videos;
    for (size_t i = 0; i < videos.size(); ++i)
    {
        if (videos[i].name != _itemName)
            continue;
        _taken.videoTaken = true;
        _taken.videoIndex = i;
        _taken.video = videos[i];
        videos.erase(videos.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    // Its data, unless another item shares it.
    auto& files = _document->extraFiles;
    const bool shared = std::any_of(media.begin(), media.end(), [&dataPath](const std::string& xml) {
        const size_t slash = dataPath.find('/');
        return slash != std::string::npos && xml.find("\"" + media::escaped(dataPath.substr(slash + 1)) + "\"") != std::string::npos;
    });
    for (size_t i = 0; !dataPath.empty() && !shared && i < files.size(); ++i)
    {
        if (files[i].first != dataPath)
            continue;
        _taken.fileTaken = true;
        _taken.fileIndex = i;
        _taken.file = files[i];
        files.erase(files.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
}

void RemoveMediaCommand::undo()
{
    if (!_document)
        return;
    const auto putBack = [](auto& list, size_t index, const auto& value) {
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index, list.size())), value);
    };
    if (_taken.fileTaken)
        putBack(_document->extraFiles, _taken.fileIndex, _taken.file);
    if (_taken.videoTaken)
        putBack(_document->videos, _taken.videoIndex, _taken.video);
    if (_taken.soundTaken)
        putBack(_document->sounds, _taken.soundIndex, _taken.sound);
    if (_taken.xmlTaken)
        putBack(_document->extraMedia, _taken.xmlIndex, _taken.xml);
    _taken = Taken();
}

} // namespace fla
