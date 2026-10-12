#include "frame_sound.h"

#include "../data/document.h"
#include "../data/frame.h"

#include <algorithm>
#include <cstdlib>

namespace fla {

namespace frame_sound {

const char* const kEffects[8] = {"none", "left channel", "right channel", "fade left to right",
    "fade right to left", "fade in", "fade out", "custom"};

const char* const kSyncs[4] = {"event", "start", "stop", "stream"};

namespace {

/// The attributes a sound setting is made of. The in and out points and the
/// zoom belong to the sound they were set for.
const char* const kSoundAttributes[] = {"soundName", "soundEffect", "soundSync", "soundLoopMode", "soundLoop"};
const char* const kSoundAttributesOfName[] = {"inPoint44", "outPoint44", "soundZoomLevel"};

template <size_t N>
bool among(const std::string& name, const char* const (&names)[N])
{
    return std::find_if(std::begin(names), std::end(names),
        [&name](const char* each) { return name == each; }) != std::end(names);
}

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
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '&')
        {
            static const std::pair<const char*, char> entities[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            bool matched = false;
            for (const auto& entity : entities)
            {
                const std::string code = entity.first;
                if (text.compare(i, code.size(), code) == 0)
                {
                    out += entity.second;
                    i += code.size() - 1;
                    matched = true;
                    break;
                }
            }
            if (matched)
                continue;
        }
        out += text[i];
    }
    return out;
}

bool isEnvelope(const std::string& child)
{
    return child.find("<SoundEnvelope") != std::string::npos;
}

std::string point(size_t mark, int left, int right)
{
    std::string xml = "<SoundEnvelopePoint";
    if (mark > 0)
        xml += " mark44=\"" + std::to_string(mark) + "\"";
    if (left > 0)
        xml += " level0=\"" + std::to_string(left) + "\"";
    if (right > 0)
        xml += " level1=\"" + std::to_string(right) + "\"";
    return xml + "/>";
}

/// The envelope of one of Animate's effects; empty for none.
std::string envelopeOf(const std::string& effect, size_t length)
{
    const int full = 32768;
    std::vector<std::string> points;
    if (effect == "left channel")
        points = {point(0, full, 0)};
    else if (effect == "right channel")
        points = {point(0, 0, full)};
    else if (effect == "fade left to right")
        points = {point(0, full, 0), point(length, 0, full)};
    else if (effect == "fade right to left")
        points = {point(0, 0, full), point(length, full, 0)};
    // The fades in and out take a quarter of the sound.
    else if (effect == "fade in")
        points = {point(0, 0, 0), point(length / 4, full, full)};
    else if (effect == "fade out")
        points = {point(length - length / 4, full, full), point(length, 0, 0)};
    if (points.empty())
        return std::string();
    std::string xml = "<SoundEnvelope>";
    for (const std::string& each : points)
        xml += each;
    return xml + "</SoundEnvelope>";
}

} // namespace

Settings read(const Frame& frame)
{
    Settings settings;
    for (const auto& attribute : frame.extraAttributes)
    {
        const std::string& name = attribute.first;
        const std::string& value = attribute.second;
        if (name == "soundName")
            settings.name = value;
        else if (name == "soundEffect")
            settings.effect = value;
        else if (name == "soundSync")
            settings.sync = value;
        else if (name == "soundLoopMode")
            settings.loop = value == "loop";
        else if (name == "soundLoop")
            settings.loops = std::max(1, std::atoi(value.c_str()));
    }
    return settings;
}

void write(const Settings& settings, size_t sampleCount,
    std::vector<std::pair<std::string, std::string>>& attributes, std::vector<std::string>& children)
{
    std::string previous;
    for (const auto& attribute : attributes)
    {
        if (attribute.first == "soundName")
            previous = attribute.second;
    }
    const bool sameSound = !settings.name.empty() && previous == settings.name;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
        [sameSound](const std::pair<std::string, std::string>& attribute) {
            return among(attribute.first, kSoundAttributes) ||
                (!sameSound && among(attribute.first, kSoundAttributesOfName));
        }), attributes.end());

    const bool custom = sameSound && settings.effect == "custom";
    if (!custom)
        children.erase(std::remove_if(children.begin(), children.end(), isEnvelope), children.end());
    if (settings.name.empty())
        return;

    // In the order Animate writes them, leaving out its defaults.
    attributes.emplace_back("soundName", settings.name);
    if (settings.effect != "none" && (settings.effect != "custom" || custom))
        attributes.emplace_back("soundEffect", settings.effect);
    if (settings.sync != "event")
        attributes.emplace_back("soundSync", settings.sync);
    if (settings.loop)
        attributes.emplace_back("soundLoopMode", "loop");
    else if (settings.loops > 1)
        attributes.emplace_back("soundLoop", std::to_string(settings.loops));

    if (!custom)
    {
        const std::string envelope = envelopeOf(settings.effect, sampleCount);
        if (!envelope.empty())
            children.insert(children.begin(), envelope);
    }
}

size_t sampleCount(const Document& document, const std::string& name)
{
    for (const std::string& item : document.extraMedia)
    {
        if (item.find("<DOMSoundItem") == std::string::npos)
            continue;
        const size_t end = item.find('>');
        const std::string tag = item.substr(0, end);
        if (unescaped(attributeOf(tag, "name")) != name)
            continue;
        // The count is at the sound's own rate; envelopes measure at 44.1 kHz.
        const long long count = std::max(0LL, std::atoll(attributeOf(tag, "sampleCount").c_str()));
        const std::string format = attributeOf(tag, "format");
        const int scale = format.rfind("5kHz", 0) == 0 ? 8 : format.rfind("11kHz", 0) == 0 ? 4
            : format.rfind("22kHz", 0) == 0 ? 2 : 1;
        return static_cast<size_t>(count) * static_cast<size_t>(scale);
    }
    return 0;
}

} // namespace frame_sound

} // namespace fla
