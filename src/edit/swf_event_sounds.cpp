#include "swf_event_sounds.h"

#include "frame_sound.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../gui/audio_engine.h"
#include "../util/swf_sound.h"
#include "../util/swf_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fla {

namespace {

std::string attributeOf(const std::string& tag, const std::string& name)
{
    const std::string key = " " + name + "=\"";
    const size_t at = tag.find(key);
    if (at == std::string::npos)
        return std::string();
    const size_t start = at + key.size();
    const size_t end = tag.find('"', start);
    return end == std::string::npos ? std::string() : tag.substr(start, end - start);
}

std::string unescaped(const std::string& text)
{
    static const std::pair<const char*, char> kEntities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        bool replaced = false;
        if (text[i] == '&')
        {
            for (const auto& [entity, character] : kEntities)
            {
                const size_t length = std::char_traits<char>::length(entity);
                if (text.compare(i, length, entity) == 0)
                {
                    out += character;
                    i += length - 1;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            out += text[i];
    }
    return out;
}

/// The library item of sound \a name, its opening tag; empty when there is
/// none.
std::string itemTag(const Document& document, const std::string& name)
{
    for (const std::string& item : document.extraMedia)
    {
        if (item.find("<DOMSoundItem") == std::string::npos)
            continue;
        const std::string tag = item.substr(0, item.find('>'));
        if (unescaped(attributeOf(tag, "name")) == name)
            return tag;
    }
    return std::string();
}

/// The SWF rate nearest below \a rate: 5512, 11025, 22050 or 44100.
int swfRate(int rate)
{
    return rate >= 44100 ? 44100 : rate >= 22050 ? 22050 : rate >= 11025 ? 11025 : 5512;
}

/// An MP3's frames as SWF takes them: from the first frame on, its rate and
/// channels from that frame's header and its samples a channel. False when
/// it is not an MP3, or at a rate SWF has no code for.
bool mp3Layout(const std::vector<uint8_t>& data, std::vector<uint8_t>& frames, int& rate, bool& stereo,
    uint32_t& samples)
{
    const std::vector<swf::Mp3Frame> found = swf::mp3Frames(data);
    if (found.empty())
        return false;
    const size_t at = found.front().offset;
    const int version = (data[at + 1] >> 3) & 3; // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
    const int index = (data[at + 2] >> 2) & 3;
    static const int kRates[4][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}, {44100, 48000, 32000}};
    rate = index < 3 ? kRates[version][index] : 0;
    if (rate != 11025 && rate != 22050 && rate != 44100)
        return false;
    stereo = (data[at + 3] >> 6) != 3;
    samples = 0;
    for (const swf::Mp3Frame& frame : found)
        samples += static_cast<uint32_t>(frame.samples);
    const swf::Mp3Frame& last = found.back();
    frames.assign(data.begin() + static_cast<std::ptrdiff_t>(at),
        data.begin() + static_cast<std::ptrdiff_t>(last.offset + last.length));
    return true;
}

/// A timeline's length: the furthest any of its layers reaches.
int lengthOf(const std::vector<const Timeline*>& timelines)
{
    int length = 1;
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                if (frame)
                    length = std::max(length, frame->index + std::max(frame->duration, 1));
    return length;
}

/// The movie clips among \a element, inside groups too, in order.
void clipsIn(const Element* element, std::vector<const SymbolInstance*>& out)
{
    if (!element)
        return;
    if (element->elementType() == Element::Type::Group)
    {
        for (const Element* member : static_cast<const Group*>(element)->members)
            clipsIn(member, out);
        return;
    }
    if (element->elementType() != Element::Type::SymbolInstance)
        return;
    const SymbolInstance* instance = static_cast<const SymbolInstance*>(element);
    if (instance->symbolType == SymbolType::MovieClip && instance->symbol)
        out.push_back(instance);
}

/// Whether a keyframe's sound plays as an event (event, start or stop).
bool eventSound(const Frame& key)
{
    const frame_sound::Settings sound = frame_sound::read(key);
    return !sound.name.empty() && (sound.sync == "event" || sound.sync == "start" || sound.sync == "stop");
}

/// Movie clips inside movie clips, this deep at most.
constexpr int kMaxNesting = 16;

} // namespace

SwfEventSounds::SwfEventSounds(const Document& document, const publish::Settings& settings, Mp3Encoder mp3)
    : _document(document)
    , _settings(settings)
    , _mp3(std::move(mp3))
{
}

bool SwfEventSounds::itemFormat(const Document& document, const std::string& name,
    publish::Settings::SoundFormat& format, bool& native)
{
    const std::string tag = itemTag(document, name);
    native = attributeOf(tag, "exportNative") == "true";
    const std::string bits = attributeOf(tag, "exportBits");
    if (bits.empty())
        return false;
    format = publish::soundFormat(std::atoi(attributeOf(tag, "exportFormat").c_str()), std::atoi(bits.c_str()));
    return true;
}

std::shared_ptr<const PcmBuffer> SwfEventSounds::pcmOf(const std::string& name)
{
    const auto cached = _pcm.find(name);
    if (cached != _pcm.end())
        return cached->second;
    std::shared_ptr<const PcmBuffer> pcm;
    for (const Document::SoundInfo& sound : _document.sounds)
    {
        if (sound.name != name)
            continue;
        for (const auto& file : _document.extraFiles)
            if (file.first == sound.dataPath)
            {
                pcm = AudioDecode::decode(file.second.get(), sound.format);
                break;
            }
        break;
    }
    _pcm[name] = pcm;
    return pcm;
}

int SwfEventSounds::soundOf(const std::string& name)
{
    const auto known = _sounds.find(name);
    if (known != _sounds.end())
        return known->second;
    _sounds[name] = 0;
    std::shared_ptr<const PcmBuffer> pcm = pcmOf(name);
    if (!pcm || pcm->frames() == 0 || !_writer)
        return 0;

    publish::Settings::SoundFormat format = _settings.event;
    bool native = false;
    if (!_settings.overrideSounds)
    {
        publish::Settings::SoundFormat own;
        if (itemFormat(_document, name, own, native))
            format = own;
    }
    if (format.compression == "none")
        return 0;

    int id = 0;
    // An MP3 kept at its imported quality: its frames as they are.
    if (native)
    {
        for (const Document::SoundInfo& sound : _document.sounds)
        {
            if (sound.name != name)
                continue;
            for (const auto& file : _document.extraFiles)
            {
                std::vector<uint8_t> frames;
                int rate = 0;
                bool stereo = false;
                uint32_t samples = 0;
                if (file.first != sound.dataPath || !mp3Layout(file.second.get(), frames, rate, stereo, samples))
                    continue;
                std::vector<uint8_t> data{0, 0}; // no seek
                data.insert(data.end(), frames.begin(), frames.end());
                id = _writer->defineSound(2, rate, stereo, samples, data);
                break;
            }
            break;
        }
    }
    const int channels = format.stereo ? 2 : 1;
    if (!id && format.compression == "mp3")
    {
        // MP3 has no 5.5 kHz.
        const int rate = std::max(swfRate(format.rate), 11025);
        std::vector<uint8_t> mp3;
        if (_mp3 && _mp3(pcm->samples, rate, channels, std::clamp(format.bitRate, 8, 320), mp3) && !mp3.empty())
        {
            // An event MP3 starts with its seek: LAME's own 1105 samples.
            std::vector<uint8_t> data{0x51, 0x04};
            data.insert(data.end(), mp3.begin(), mp3.end());
            const uint32_t samples = static_cast<uint32_t>(
                std::llround(static_cast<double>(pcm->frames()) * rate / PcmBuffer::kRate));
            id = _writer->defineSound(2, rate, channels == 2, samples, data);
        }
        else
            _mp3Missing = true;
    }
    if (!id)
    {
        const int rate = swfRate(format.rate);
        const std::vector<int16_t> samples = swf::pcm16(pcm->samples, PcmBuffer::kRate, rate, channels);
        const uint32_t count = static_cast<uint32_t>(samples.size() / static_cast<size_t>(channels));
        if (format.compression == "raw")
        {
            std::vector<uint8_t> data;
            data.reserve(samples.size() * 2);
            for (const int16_t sample : samples)
            {
                data.push_back(static_cast<uint8_t>(sample & 0xFF));
                data.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
            }
            id = _writer->defineSound(3, rate, channels == 2, count, data);
        }
        else
            id = _writer->defineSound(1, rate, channels == 2, count,
                swf::encodeAdpcm(samples, channels, std::clamp(format.adpcmBits, 2, 5)));
    }
    _sounds[name] = id;
    return id;
}

bool SwfEventSounds::soundInfo(const Frame& key, swf::SoundInfo& info)
{
    const frame_sound::Settings sound = frame_sound::read(key);
    info = swf::SoundInfo();
    if (sound.sync == "stop")
    {
        info.stop = true;
        return true;
    }
    std::shared_ptr<const PcmBuffer> pcm = pcmOf(sound.name);
    if (!pcm)
        return false;
    // The part played, as playback trims it.
    const size_t length = pcm->frames();
    size_t in = 0;
    size_t out = 0;
    frame_sound::trim(key, in, out);
    in = std::min(in, length);
    out = out > in ? std::min(out, length) : length;
    if (out <= in)
        return false;
    info.noMultiple = sound.sync == "start";
    info.loops = sound.loop ? 0 : sound.loops;
    info.inPoint = static_cast<uint32_t>(in);
    info.outPoint = out < length ? static_cast<uint32_t>(out) : 0;
    const std::vector<frame_sound::EnvelopePoint> points = frame_sound::envelope(key);
    const bool flat = std::all_of(points.begin(), points.end(),
        [](const frame_sound::EnvelopePoint& point) { return point.left == 32768 && point.right == 32768; });
    if (!flat)
        for (const frame_sound::EnvelopePoint& point : points)
            info.envelope.push_back({static_cast<uint32_t>(std::min<size_t>(point.mark, 0xFFFFFFFFu)),
                static_cast<uint16_t>(std::clamp(point.left, 0, 32768)),
                static_cast<uint16_t>(std::clamp(point.right, 0, 32768))});
    return true;
}

std::map<int, std::vector<SwfEventSounds::Action>> SwfEventSounds::actions(
    const std::vector<const Timeline*>& timelines, int length, int firstDepth, int step, int level)
{
    std::map<int, std::vector<Action>> out;
    // A depth for each clip a layer places, by its place among the layer's
    // clips, so a keyframe's clips take over from the last one's.
    std::map<std::pair<const Layer*, size_t>, int> depths;
    int next = firstDepth;
    for (const Timeline* timeline : timelines)
    {
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
        {
            if (!layer || !layer->visible)
                continue;
            for (const Frame* key : layer->frames)
            {
                if (!key || key->index < 0 || key->index >= length)
                    continue;
                if (eventSound(*key))
                {
                    Action action;
                    action.key = key;
                    out[key->index].push_back(action);
                }
                std::vector<const SymbolInstance*> clips;
                for (const Element* element : key->elements)
                    clipsIn(element, clips);
                for (size_t i = 0; i < clips.size(); ++i)
                {
                    const int id = spriteOf(clips[i]->symbol, level + 1);
                    if (!id)
                        continue;
                    auto depth = depths.find({layer, i});
                    if (depth == depths.end())
                    {
                        depth = depths.emplace(std::make_pair(layer, i), next).first;
                        next += step;
                    }
                    Action place;
                    place.kind = Action::Kind::Place;
                    place.id = id;
                    place.depth = depth->second;
                    out[key->index].push_back(place);
                    const int end = key->index + std::max(key->duration, 1);
                    if (end < length)
                    {
                        Action remove;
                        remove.kind = Action::Kind::Remove;
                        remove.depth = depth->second;
                        // Gone before the next keyframe's clips come on.
                        std::vector<Action>& there = out[end];
                        there.insert(std::find_if(there.begin(), there.end(),
                                         [](const Action& a) { return a.kind != Action::Kind::Remove; }),
                            remove);
                    }
                }
            }
        }
    }
    return out;
}

int SwfEventSounds::spriteOf(const Symbol* symbol, int level)
{
    if (!symbol || level > kMaxNesting)
        return 0;
    const auto known = _sprites.find(symbol);
    if (known != _sprites.end())
        return known->second;
    // A clip inside itself sounds once.
    _sprites[symbol] = 0;
    std::vector<const Timeline*> timelines(symbol->timelines.begin(), symbol->timelines.end());
    const int length = lengthOf(timelines);
    const std::map<int, std::vector<Action>> plan = actions(timelines, length, 1, 1, level);
    swf::Sprite sprite;
    bool sounds = false;
    for (int frame = 0; frame < length; ++frame)
    {
        const auto here = plan.find(frame);
        if (here != plan.end())
            for (const Action& action : here->second)
            {
                swf::SoundInfo info;
                switch (action.kind)
                {
                case Action::Kind::Sound:
                {
                    const int id = soundOf(frame_sound::read(*action.key).name);
                    if (id && soundInfo(*action.key, info))
                    {
                        sprite.startSound(id, info);
                        sounds = true;
                    }
                    break;
                }
                case Action::Kind::Place:
                    sprite.place(action.id, action.depth);
                    sounds = true;
                    break;
                case Action::Kind::Remove:
                    sprite.remove(action.depth);
                    break;
                }
            }
        sprite.showFrame();
    }
    const int id = sounds && _writer ? _writer->defineSprite(sprite) : 0;
    _sprites[symbol] = id;
    return id;
}

void SwfEventSounds::define(swf::Writer& writer, int frames)
{
    _writer = &writer;
    _scene.clear();
    const Timeline* scene = _document.currentScene();
    if (!scene || frames <= 0)
        return;
    // The scene's clips at the top of the depths, out of the way of what
    // the movie draws from the bottom up.
    _scene = actions({scene}, frames, 65535, -1, 0);
    // Each sound written before the frame that first starts it.
    for (const auto& [frame, list] : _scene)
        for (const Action& action : list)
            if (action.kind == Action::Kind::Sound)
                soundOf(frame_sound::read(*action.key).name);
}

void SwfEventSounds::frame(swf::Writer& writer, int frame)
{
    const auto here = _scene.find(frame);
    if (here == _scene.end())
        return;
    for (const Action& action : here->second)
    {
        swf::SoundInfo info;
        switch (action.kind)
        {
        case Action::Kind::Sound:
        {
            const int id = soundOf(frame_sound::read(*action.key).name);
            if (id && soundInfo(*action.key, info))
                writer.startSound(id, info);
            break;
        }
        case Action::Kind::Place:
            writer.place(action.id, action.depth);
            break;
        case Action::Kind::Remove:
            writer.remove(action.depth);
            break;
        }
    }
}

} // namespace fla
