#include "frame_sound.h"

#include "timeline_edit.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"

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

/// The envelope of one of the sound effects; empty for none.
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

    // In the order FLA files use, leaving out the defaults.
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

std::vector<EnvelopePoint> envelope(const Frame& frame)
{
    std::vector<EnvelopePoint> points;
    for (const std::string& child : frame.extraChildren)
    {
        if (!isEnvelope(child))
            continue;
        size_t at = 0;
        while ((at = child.find("<SoundEnvelopePoint", at)) != std::string::npos)
        {
            const size_t end = child.find('>', at);
            const std::string tag = child.substr(at, end - at);
            EnvelopePoint point;
            point.mark = static_cast<size_t>(std::max(0LL, std::atoll(attributeOf(tag, "mark44").c_str())));
            // A level left out is silence.
            point.left = std::atoi(attributeOf(tag, "level0").c_str());
            point.right = std::atoi(attributeOf(tag, "level1").c_str());
            points.push_back(point);
            at = end;
        }
    }
    std::stable_sort(points.begin(), points.end(),
        [](const EnvelopePoint& a, const EnvelopePoint& b) { return a.mark < b.mark; });
    return points;
}

void trim(const Frame& frame, size_t& in, size_t& out)
{
    in = 0;
    out = 0;
    for (const auto& attribute : frame.extraAttributes)
    {
        if (attribute.first == "inPoint44")
            in = static_cast<size_t>(std::max(0LL, std::atoll(attribute.second.c_str())));
        else if (attribute.first == "outPoint44")
            out = static_cast<size_t>(std::max(0LL, std::atoll(attribute.second.c_str())));
    }
}

void setEnvelope(const std::vector<EnvelopePoint>& points, size_t in, size_t out,
    std::vector<std::pair<std::string, std::string>>& attributes, std::vector<std::string>& children)
{
    children.erase(std::remove_if(children.begin(), children.end(), isEnvelope), children.end());
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& attribute) {
                             return attribute.first == "soundEffect" || attribute.first == "inPoint44" ||
                                 attribute.first == "outPoint44";
                         }),
        attributes.end());
    // Next to the sound's name, as FLA files order them.
    auto after = std::find_if(attributes.begin(), attributes.end(),
        [](const std::pair<std::string, std::string>& attribute) { return attribute.first == "soundName"; });
    std::vector<std::pair<std::string, std::string>> added;
    if (!points.empty())
        added.emplace_back("soundEffect", "custom");
    if (in > 0)
        added.emplace_back("inPoint44", std::to_string(in));
    if (out > 0)
        added.emplace_back("outPoint44", std::to_string(out));
    attributes.insert(after == attributes.end() ? attributes.end() : after + 1, added.begin(), added.end());
    if (points.empty())
        return;
    std::string xml = "<SoundEnvelope>";
    for (const EnvelopePoint& each : points)
        xml += point(each.mark, each.left, each.right);
    children.insert(children.begin(), xml + "</SoundEnvelope>");
}

bool splitAudio(Layer& layer, int frame, double frameRate)
{
    Frame* first = timeline::spanAt(layer, frame);
    if (!first || first->index == frame || frameRate <= 0.0)
        return false;
    const Settings settings = read(*first);
    if (settings.name.empty() || settings.sync != "stream")
        return false;
    size_t in = 0, out = 0;
    trim(*first, in, out);
    const size_t at = in + static_cast<size_t>((frame - first->index) * 44100.0 / frameRate + 0.5);
    if (out > 0 && at >= out)
        return false;
    // The sound's attributes and envelope as they are, before the cut.
    std::vector<std::pair<std::string, std::string>> sound;
    for (const auto& attribute : first->extraAttributes)
        if (among(attribute.first, kSoundAttributes) || among(attribute.first, kSoundAttributesOfName))
            sound.push_back(attribute);
    std::vector<std::string> envelopes;
    for (const std::string& child : first->extraChildren)
        if (isEnvelope(child))
            envelopes.push_back(child);

    Frame* second = timeline::insertKeyframe(layer, frame, false);
    if (!second || second == first)
        return false;
    const auto setPoint = [](std::vector<std::pair<std::string, std::string>>& attributes, const char* name, size_t value) {
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                             [name](const std::pair<std::string, std::string>& a) { return a.first == name; }),
            attributes.end());
        attributes.emplace_back(name, std::to_string(value));
    };
    // The second takes the sound from the cut on; the first stops there.
    auto& attributes = second->extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& a) {
                             return among(a.first, kSoundAttributes) || among(a.first, kSoundAttributesOfName);
                         }),
        attributes.end());
    attributes.insert(attributes.end(), sound.begin(), sound.end());
    setPoint(attributes, "inPoint44", at);
    second->extraChildren.erase(std::remove_if(second->extraChildren.begin(), second->extraChildren.end(), isEnvelope),
        second->extraChildren.end());
    second->extraChildren.insert(second->extraChildren.begin(), envelopes.begin(), envelopes.end());
    setPoint(first->extraAttributes, "outPoint44", at);
    return true;
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
