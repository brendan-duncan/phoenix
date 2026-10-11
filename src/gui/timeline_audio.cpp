#include "timeline_audio.h"

#include "player.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdlib>

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
    for (const fla::Timeline* timeline : doc->timelines)
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
                    // Where the sound is at this frame, and how long the
                    // span lets it run.
                    const size_t offset = in + static_cast<size_t>(frame - key->index) * samplesPerFrame;
                    const size_t spanEnd = std::min(out, in + static_cast<size_t>(std::max(key->duration, 1)) * samplesPerFrame);
                    if (offset >= spanEnd)
                        continue;
                    if (!_playing)
                    {
                        // A scrub: a frame's worth, so the sound can be found.
                        _scrubVoice = engine.play(pcm, offset, std::min(spanEnd, offset + samplesPerFrame), 1, 1.0f, sound.envelope);
                        continue;
                    }
                    const auto running = _streams.find(key);
                    if (running != _streams.end() && engine.isPlaying(running->second))
                    {
                        stillStreaming[key] = running->second;
                    }
                    else
                    {
                        const int voice = engine.play(pcm, offset, spanEnd, 1, 1.0f, sound.envelope);
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
