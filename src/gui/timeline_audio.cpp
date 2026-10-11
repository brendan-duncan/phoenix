#include "timeline_audio.h"

#include "player.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

TimelineAudio::TimelineAudio(Player* player, QObject* parent)
    : QObject(parent)
    , _player(player)
{
}

void TimelineAudio::setDocument(const fla::FLADocument* document)
{
    AudioEngine::instance().stopAll();
    _document = document;
    _decoded.clear();
    _streams.clear();
    _events.clear();
    _scrubVoice = -1;
    _lastFrame = -1;
}

void TimelineAudio::setMuted(bool muted)
{
    _muted = muted;
    if (muted)
    {
        AudioEngine::instance().stopAll();
        _streams.clear();
        _events.clear();
    }
}

double TimelineAudio::frameRate() const
{
    const fla::Document* doc = _document ? _document->document : nullptr;
    return doc && doc->frameRate > 0.0 ? doc->frameRate : 24.0;
}

bool TimelineAudio::soundSettings(const fla::Frame& frame, FrameSound& sound)
{
    std::string loopMode;
    for (const auto& attribute : frame.extraAttributes)
    {
        const std::string& name = attribute.first;
        const std::string& value = attribute.second;
        if (name == "soundName")
            sound.name = value;
        else if (name == "soundSync")
            sound.sync = value;
        else if (name == "inPoint44")
            sound.in = static_cast<size_t>(std::max(0L, std::atol(value.c_str())));
        else if (name == "outPoint44")
        {
            sound.out = static_cast<size_t>(std::max(0L, std::atol(value.c_str())));
            sound.hasOut = true;
        }
        else if (name == "soundLoop")
            sound.loops = std::max(1, std::atoi(value.c_str()));
        else if (name == "soundLoopMode")
            loopMode = value;
    }
    if (loopMode == "loop")
        sound.loops = 0;
    for (const std::string& child : frame.extraChildren)
    {
        if (child.find("<SoundEnvelope") != std::string::npos)
            sound.envelope = AudioEnvelope::parse(child);
    }
    // Animate leaves the sync out for an event sound, the default.
    if (sound.sync.empty())
        sound.sync = "event";
    return !sound.name.empty();
}

std::shared_ptr<const PcmBuffer> TimelineAudio::buffer(const std::string& name)
{
    const auto cached = _decoded.find(name);
    if (cached != _decoded.end())
        return cached->second;
    std::shared_ptr<const PcmBuffer> decoded;
    const fla::Document* doc = _document ? _document->document : nullptr;
    if (doc)
    {
        for (const fla::Document::SoundInfo& sound : doc->sounds)
        {
            if (sound.name != name)
                continue;
            for (const auto& file : doc->extraFiles)
            {
                if (file.first == sound.dataPath)
                {
                    decoded = AudioDecode::decode(file.second, sound.format);
                    break;
                }
            }
            break;
        }
    }
    _decoded[name] = decoded;
    return decoded;
}

std::shared_ptr<const PcmBuffer> TimelineAudio::soundOf(const fla::Frame* frame, size_t* in)
{
    FrameSound sound;
    if (!frame || !soundSettings(*frame, sound) || sound.sync == "stop")
        return nullptr;
    if (in)
        *in = sound.in;
    return buffer(sound.name);
}

std::vector<float> TimelineAudio::mixdown(int first, int last)
{
    std::vector<float> mixed;
    const fla::Document* doc = _document ? _document->document : nullptr;
    if (!doc || last < first)
        return mixed;
    const double samplesPerFrame = PcmBuffer::kRate / frameRate();
    const auto at = [&](int frame) { return static_cast<size_t>(std::llround((frame - first) * samplesPerFrame)); };
    const size_t total = at(last + 1);
    mixed.assign(total * 2, 0.0f);

    // Each sound placed on the output: from where, which part of it, how
    // many times round (0 for until stopped), how far into its playing it
    // starts (a stream picked up part way), and where a stop cuts it.
    struct Placed
    {
        std::string name;
        std::shared_ptr<const PcmBuffer> pcm;
        size_t start = 0;
        size_t in = 0;
        size_t out = 0;
        int loops = 1;
        size_t skip = 0;
        size_t cut = static_cast<size_t>(-1);
        std::shared_ptr<const AudioEnvelope> envelope;

        size_t end() const
        {
            const size_t length = out - in;
            const size_t natural = loops > 0 ? start + length * static_cast<size_t>(loops) - skip : static_cast<size_t>(-1);
            return std::min(natural, cut);
        }
    };
    std::vector<Placed> placed;

    // The timelines sounding at a frame: the scene's, and those of the movie
    // clips on it, which an exported movie plays round and round from when
    // they appear (as the view draws them), down through groups and nested
    // clips. A graphic symbol's own sounds stay silent, as in Animate.
    struct Sounding
    {
        const fla::Timeline* timeline;
        int frame;
    };
    std::function<void(const fla::Timeline*, int, std::vector<Sounding>&)> collect;
    const std::function<void(const fla::Element*, int, std::vector<Sounding>&)> clipsIn =
        [&](const fla::Element* element, int elapsed, std::vector<Sounding>& out) {
            if (!element)
                return;
            if (element->elementType() == fla::Element::Type::Group)
            {
                for (const fla::Element* member : static_cast<const fla::Group*>(element)->members)
                    clipsIn(member, elapsed, out);
                return;
            }
            if (element->elementType() != fla::Element::Type::SymbolInstance)
                return;
            const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
            if (instance->symbolType != fla::SymbolType::MovieClip || !instance->symbol)
                return;
            int length = 1;
            for (const fla::Timeline* timeline : instance->symbol->timelines)
            {
                for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
                {
                    for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                    {
                        if (frame)
                            length = std::max(length, frame->index + std::max(frame->duration, 1));
                    }
                }
            }
            for (const fla::Timeline* timeline : instance->symbol->timelines)
                collect(timeline, elapsed % length, out);
        };
    collect = [&](const fla::Timeline* timeline, int frame, std::vector<Sounding>& out) {
        if (!timeline || out.size() > 256)
            return;
        out.push_back({timeline, frame});
        for (const fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->visible)
                continue;
            for (const fla::Frame* key : layer->frames)
            {
                if (!key || key->index > frame || frame >= key->index + std::max(key->duration, 1))
                    continue;
                for (const fla::Element* element : key->elements)
                    clipsIn(element, frame - key->index, out);
            }
        }
    };

    for (int frame = first; frame <= last; ++frame)
    {
        const size_t here = at(frame);
        std::vector<Sounding> sounding;
        collect(doc->currentScene(), frame, sounding);
        for (const Sounding& where : sounding)
        {
            const int local = where.frame;
            for (const fla::Layer* layer : where.timeline->layers)
            {
                if (!layer || !layer->visible)
                    continue;
                for (const fla::Frame* key : layer->frames)
                {
                    if (!key || key->index > local || local >= key->index + std::max(key->duration, 1))
                        continue;
                    FrameSound sound;
                    if (!soundSettings(*key, sound))
                        continue;
                    // A stream is placed from the start of its span, or from
                    // part way in at the first frame of the range.
                    const bool streamStart = sound.sync == "stream" && (local == key->index || frame == first);
                    if (key->index != local && !streamStart)
                        continue;
                    if (sound.sync == "stop")
                    {
                        for (Placed& voice : placed)
                        {
                            if (voice.name == sound.name && voice.start <= here && here < voice.end())
                                voice.cut = here;
                        }
                        continue;
                    }
                    std::shared_ptr<const PcmBuffer> pcm = buffer(sound.name);
                    if (!pcm)
                        continue;
                    Placed voice;
                    voice.name = sound.name;
                    voice.pcm = pcm;
                    voice.start = here;
                    voice.in = std::min(sound.in, pcm->frames());
                    voice.out = sound.hasOut && sound.out > voice.in ? std::min(sound.out, pcm->frames()) : pcm->frames();
                    voice.loops = sound.loops;
                    voice.envelope = sound.envelope;
                    if (voice.out <= voice.in)
                        continue;
                    if (sound.sync == "stream")
                    {
                        // From where the span is at this frame, repeating as
                        // told, for as long as the span runs.
                        voice.skip = static_cast<size_t>(std::llround((local - key->index) * samplesPerFrame));
                        voice.cut = at(frame + key->index + std::max(key->duration, 1) - local);
                        if (voice.loops > 0 && voice.skip >= (voice.out - voice.in) * static_cast<size_t>(voice.loops))
                            continue;
                    }
                    else if (sound.sync == "start")
                    {
                        bool already = false;
                        for (const Placed& other : placed)
                            already = already || (other.name == sound.name && other.start <= here && here < other.end());
                        if (already)
                            continue;
                    }
                    placed.push_back(voice);
                }
            }
        }
    }

    for (const Placed& voice : placed)
    {
        const size_t length = voice.out - voice.in;
        const size_t end = std::min(voice.end(), total);
        for (size_t at = voice.start; at < end; ++at)
        {
            const size_t played = at - voice.start + voice.skip;
            const size_t source = voice.in + played % length;
            float left = 1.0f;
            float right = 1.0f;
            if (voice.envelope)
                voice.envelope->gainAt(voice.in + played, left, right);
            mixed[at * 2] += voice.pcm->samples[source * 2] * left;
            mixed[at * 2 + 1] += voice.pcm->samples[source * 2 + 1] * right;
        }
    }
    for (float& sample : mixed)
        sample = std::max(-1.0f, std::min(1.0f, sample));
    return mixed;
}

void TimelineAudio::stopStreams()
{
    for (const auto& entry : _streams)
        AudioEngine::instance().stop(entry.second);
    _streams.clear();
}

void TimelineAudio::onPlayingChanged(bool playing)
{
    _playing = playing;
    AudioEngine::instance().stopAll();
    _streams.clear();
    _events.clear();
    _scrubVoice = -1;
    _lastFrame = -1;
    if (playing && _player)
        onFrameChanged(_player->currentFrame());
}

void TimelineAudio::onFrameChanged(int frame)
{
    const fla::Document* doc = _document ? _document->document : nullptr;
    if (!doc || doc->sounds.empty() || _muted)
        return;

    AudioEngine& engine = AudioEngine::instance();
    const double fps = frameRate();
    const size_t samplesPerFrame = static_cast<size_t>(PcmBuffer::kRate / fps);
    // A jump back, as at a loop or a click behind the playhead, starts the
    // streams over from where the playhead now is. Skipping ahead does
    // not: playback drops frames it cannot draw in time, and the sound
    // and the playhead keep the same clock.
    const bool jumped = _lastFrame < 0 || frame < _lastFrame;
    if (_playing && jumped)
        stopStreams();

    if (!_playing && _scrubVoice >= 0)
    {
        engine.stop(_scrubVoice);
        _scrubVoice = -1;
    }

    std::map<const fla::Frame*, int> stillStreaming;
    for (const fla::Timeline* timeline : {static_cast<const fla::Timeline*>(doc->currentScene())})
    {
        if (!timeline)
            continue;
        for (const fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->visible)
                continue;
            for (const fla::Frame* key : layer->frames)
            {
                if (!key || key->index > frame || frame >= key->index + std::max(key->duration, 1))
                    continue;
                FrameSound sound;
                if (!soundSettings(*key, sound))
                    continue;

                if (sound.sync == "stop")
                {
                    if (_playing && key->index == frame)
                    {
                        const auto range = _events.equal_range(sound.name);
                        for (auto it = range.first; it != range.second; ++it)
                            engine.stop(it->second);
                        _events.erase(range.first, range.second);
                    }
                    continue;
                }

                std::shared_ptr<const PcmBuffer> pcm = buffer(sound.name);
                if (!pcm)
                    continue;
                const size_t in = std::min(sound.in, pcm->frames());
                const size_t out = sound.hasOut && sound.out > in ? std::min(sound.out, pcm->frames()) : pcm->frames();

                if (sound.sync == "stream")
                {
                    // How far into its playing the sound is at this frame,
                    // its repeats counted: a stream repeats as told for as
                    // long as its span lasts.
                    const size_t length = out - in;
                    const size_t elapsed = static_cast<size_t>(frame - key->index) * samplesPerFrame;
                    if (length == 0 || (sound.loops > 0 && elapsed >= length * static_cast<size_t>(sound.loops)))
                        continue;
                    if (!_playing)
                    {
                        // A scrub: a frame's worth, so the sound can be found.
                        const size_t offset = in + elapsed % length;
                        _scrubVoice = engine.play(pcm, offset, std::min(out, offset + samplesPerFrame), 1, 1.0f, sound.envelope);
                        continue;
                    }
                    const auto running = _streams.find(key);
                    if (running != _streams.end() && engine.isPlaying(running->second))
                    {
                        stillStreaming[key] = running->second;
                    }
                    else
                    {
                        // It stops when the playhead leaves the span (below).
                        const int voice = engine.play(pcm, in, out, sound.loops, 1.0f, sound.envelope, elapsed);
                        if (voice >= 0)
                            stillStreaming[key] = voice;
                    }
                    continue;
                }

                // Event and Start sounds go off as the playhead reaches
                // their keyframe.
                if (!_playing || key->index != frame)
                    continue;
                if (sound.sync == "start")
                {
                    bool already = false;
                    const auto range = _events.equal_range(sound.name);
                    for (auto it = range.first; it != range.second; ++it)
                        already = already || engine.isPlaying(it->second);
                    if (already)
                        continue;
                }
                const int voice = engine.play(pcm, in, out, sound.loops, 1.0f, sound.envelope);
                if (voice >= 0)
                    _events.emplace(sound.name, voice);
            }
        }
    }

    if (_playing)
    {
        // Streams whose span the playhead has left stop.
        for (const auto& entry : _streams)
        {
            if (!stillStreaming.count(entry.first))
                engine.stop(entry.second);
        }
        _streams = stillStreaming;
    }
    _lastFrame = frame;
}
