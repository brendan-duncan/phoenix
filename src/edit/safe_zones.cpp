#include "safe_zones.h"

#include "../data/document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fla::safezones {

const char* const kAttribute = "phoenixSafeZones";

namespace {

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4g", value);
    return buffer;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, separator))
        parts.push_back(part);
    return parts;
}

std::string hexColor(const uint8_t color[4])
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X", color[0], color[1], color[2], color[3]);
    return buffer;
}

void readColor(const std::string& text, uint8_t color[4])
{
    if (text.size() != 8)
        return;
    for (int i = 0; i < 4; ++i)
    {
        char* end = nullptr;
        const std::string pair = text.substr(static_cast<size_t>(i) * 2, 2);
        const long value = std::strtol(pair.c_str(), &end, 16);
        if (end && *end == '\0')
            color[i] = static_cast<uint8_t>(value);
    }
}

char anchorCode(Anchor anchor)
{
    switch (anchor)
    {
    case Anchor::Left: return 'l';
    case Anchor::Right: return 'r';
    case Anchor::Top: return 't';
    case Anchor::Bottom: return 'b';
    case Anchor::Center: break;
    }
    return 'c';
}

Anchor anchorOf(const std::string& code)
{
    if (code == "l")
        return Anchor::Left;
    if (code == "r")
        return Anchor::Right;
    if (code == "t")
        return Anchor::Top;
    if (code == "b")
        return Anchor::Bottom;
    return Anchor::Center;
}

/// Distinct colors for the frames, in the order they are listed.
const uint8_t kColors[][4] = {
    {255, 160, 0, 255},   // orange
    {0, 190, 255, 255},   // sky
    {255, 80, 160, 255},  // pink
    {160, 120, 255, 255}, // violet
    {90, 210, 90, 255},   // green
    {255, 220, 0, 255},   // yellow
    {255, 90, 60, 255},   // red
};

double clampPercent(double value)
{
    return std::isfinite(value) ? std::clamp(value, 1.0, 100.0) : 90.0;
}

} // namespace

std::string AspectFrame::name() const
{
    return number(width) + ":" + number(height);
}

bool AspectFrame::operator==(const AspectFrame& other) const
{
    return width == other.width && height == other.height && on == other.on &&
           std::equal(color, color + 4, other.color) && anchor == other.anchor && shade == other.shade;
}

bool Settings::operator==(const Settings& other) const
{
    return titleSafe == other.titleSafe && titlePercent == other.titlePercent && actionSafe == other.actionSafe &&
           actionPercent == other.actionPercent && centerCross == other.centerCross && thirds == other.thirds &&
           follow == other.follow && frames == other.frames;
}

AspectFrame frameOf(double width, double height)
{
    AspectFrame frame;
    frame.width = width;
    frame.height = height;
    return frame;
}

Settings defaults()
{
    Settings settings;
    const double ratios[][2] = {{16, 9}, {4, 3}, {1.85, 1}, {2.39, 1}, {1, 1}, {4, 5}, {9, 16}};
    for (size_t i = 0; i < std::size(ratios); ++i)
    {
        AspectFrame frame = frameOf(ratios[i][0], ratios[i][1]);
        std::copy(kColors[i % std::size(kColors)], kColors[i % std::size(kColors)] + 4, frame.color);
        settings.frames.push_back(frame);
    }
    return settings;
}

bool parseRatio(const std::string& text, double& width, double& height)
{
    std::string t;
    for (char c : text)
        if (c != ' ')
            t += c;
    if (t.empty())
        return false;
    const size_t colon = t.find_first_of(":x/");
    char* end = nullptr;
    const double w = std::strtod(t.c_str(), &end);
    if (end == t.c_str())
        return false;
    double h = 1.0;
    if (colon != std::string::npos)
    {
        const char* rest = t.c_str() + colon + 1;
        char* restEnd = nullptr;
        h = std::strtod(rest, &restEnd);
        if (restEnd == rest || *restEnd != '\0')
            return false;
    }
    else if (*end != '\0')
    {
        return false;
    }
    if (!(w > 0.0) || !(h > 0.0) || !std::isfinite(w) || !std::isfinite(h) || w / h > 100.0 || h / w > 100.0)
        return false;
    width = w;
    height = h;
    return true;
}

std::string toText(const Settings& settings)
{
    std::string text = "title=" + std::string(settings.titleSafe ? "1" : "0") + "," + number(settings.titlePercent);
    text += ";action=" + std::string(settings.actionSafe ? "1" : "0") + "," + number(settings.actionPercent);
    text += ";cross=" + std::string(settings.centerCross ? "1" : "0");
    text += ";thirds=" + std::string(settings.thirds ? "1" : "0");
    text += ";follow=" + std::to_string(settings.follow);
    text += ";frames=";
    for (size_t i = 0; i < settings.frames.size(); ++i)
    {
        const AspectFrame& frame = settings.frames[i];
        if (i > 0)
            text += "|";
        text += frame.name() + "," + (frame.on ? "1" : "0") + "," + hexColor(frame.color) + "," +
                anchorCode(frame.anchor) + "," + (frame.shade ? "1" : "0");
    }
    return text;
}

Settings fromText(const std::string& text)
{
    Settings settings = defaults();
    for (const std::string& part : split(text, ';'))
    {
        const size_t equals = part.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = part.substr(0, equals);
        const std::string value = part.substr(equals + 1);
        const std::vector<std::string> fields = split(value, ',');
        if (key == "title" && fields.size() >= 2)
        {
            settings.titleSafe = fields[0] == "1";
            settings.titlePercent = clampPercent(std::atof(fields[1].c_str()));
        }
        else if (key == "action" && fields.size() >= 2)
        {
            settings.actionSafe = fields[0] == "1";
            settings.actionPercent = clampPercent(std::atof(fields[1].c_str()));
        }
        else if (key == "cross")
        {
            settings.centerCross = value == "1";
        }
        else if (key == "thirds")
        {
            settings.thirds = value == "1";
        }
        else if (key == "follow")
        {
            settings.follow = std::atoi(value.c_str());
        }
        else if (key == "frames")
        {
            settings.frames.clear();
            for (const std::string& record : split(value, '|'))
            {
                const std::vector<std::string> f = split(record, ',');
                AspectFrame frame;
                if (f.empty() || !parseRatio(f[0], frame.width, frame.height))
                    continue;
                frame.on = f.size() > 1 && f[1] == "1";
                if (f.size() > 2)
                    readColor(f[2], frame.color);
                if (f.size() > 3)
                    frame.anchor = anchorOf(f[3]);
                frame.shade = f.size() > 4 && f[4] == "1";
                settings.frames.push_back(frame);
            }
        }
    }
    if (settings.follow < -1 || settings.follow >= static_cast<int>(settings.frames.size()))
        settings.follow = -1;
    return settings;
}

Settings of(const Document& document)
{
    for (const auto& [key, value] : document.extraAttributes)
        if (key == kAttribute)
            return fromText(value);
    return defaults();
}

std::vector<std::pair<std::string, std::string>> withSettings(const Document& document, const Settings& settings)
{
    std::vector<std::pair<std::string, std::string>> attributes = document.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) { return pair.first == kAttribute; }),
        attributes.end());
    if (settings != defaults())
        attributes.emplace_back(kAttribute, toText(settings));
    return attributes;
}

Rect frameRect(double stageWidth, double stageHeight, const AspectFrame& frame)
{
    Rect rect;
    if (!(stageWidth > 0.0) || !(stageHeight > 0.0) || !(frame.width > 0.0) || !(frame.height > 0.0))
        return rect;
    const double ratio = frame.width / frame.height;
    if (stageWidth / stageHeight > ratio)
    {
        // Narrower than the stage: full height.
        rect.height = stageHeight;
        rect.width = stageHeight * ratio;
        rect.y = 0.0;
        rect.x = frame.anchor == Anchor::Left    ? 0.0
                 : frame.anchor == Anchor::Right ? stageWidth - rect.width
                                                 : (stageWidth - rect.width) / 2.0;
    }
    else
    {
        // Wider than the stage (or the same): full width.
        rect.width = stageWidth;
        rect.height = stageWidth / ratio;
        rect.x = 0.0;
        rect.y = frame.anchor == Anchor::Top      ? 0.0
                 : frame.anchor == Anchor::Bottom ? stageHeight - rect.height
                                                  : (stageHeight - rect.height) / 2.0;
    }
    return rect;
}

Rect inset(const Rect& area, double percent)
{
    const double f = clampPercent(percent) / 100.0;
    Rect rect;
    rect.width = area.width * f;
    rect.height = area.height * f;
    rect.x = area.x + (area.width - rect.width) / 2.0;
    rect.y = area.y + (area.height - rect.height) / 2.0;
    return rect;
}

Rect zoneArea(double stageWidth, double stageHeight, const Settings& settings)
{
    if (settings.follow >= 0 && settings.follow < static_cast<int>(settings.frames.size()) &&
        settings.frames[static_cast<size_t>(settings.follow)].on)
    {
        return frameRect(stageWidth, stageHeight, settings.frames[static_cast<size_t>(settings.follow)]);
    }
    return Rect{0.0, 0.0, stageWidth, stageHeight};
}

void snapLines(double stageWidth, double stageHeight, const Settings& settings, std::vector<double>& xs,
    std::vector<double>& ys)
{
    const auto add = [&](const Rect& r) {
        if (r.width <= 0.0 || r.height <= 0.0)
            return;
        xs.push_back(r.x);
        xs.push_back(r.x + r.width);
        ys.push_back(r.y);
        ys.push_back(r.y + r.height);
    };
    const Rect area = zoneArea(stageWidth, stageHeight, settings);
    if (settings.titleSafe)
        add(inset(area, settings.titlePercent));
    if (settings.actionSafe)
        add(inset(area, settings.actionPercent));
    if (settings.centerCross || settings.thirds)
    {
        xs.push_back(area.x + area.width / 2.0);
        ys.push_back(area.y + area.height / 2.0);
    }
    if (settings.thirds)
    {
        xs.push_back(area.x + area.width / 3.0);
        xs.push_back(area.x + area.width * 2.0 / 3.0);
        ys.push_back(area.y + area.height / 3.0);
        ys.push_back(area.y + area.height * 2.0 / 3.0);
    }
    for (const AspectFrame& frame : settings.frames)
        if (frame.on)
            add(frameRect(stageWidth, stageHeight, frame));
}

std::vector<std::pair<std::string, Settings>> presets()
{
    std::vector<std::pair<std::string, Settings>> list;
    const auto with = [](std::initializer_list<const char*> on, int follow, bool shade) {
        Settings settings = defaults();
        for (AspectFrame& frame : settings.frames)
            for (const char* name : on)
                if (frame.name() == name)
                {
                    frame.on = true;
                    frame.shade = shade;
                }
        settings.follow = follow;
        return settings;
    };
    // A 16:9 show checked for a 4:3 center cut, its safe zones measured in
    // the 4:3 frame.
    list.emplace_back("HD Broadcast", with({"4:3"}, 1, false));
    // Theatrical crops, shaded outside.
    Settings cinema = with({"1.85:1", "2.39:1"}, -1, false);
    cinema.thirds = true;
    list.emplace_back("Cinema", cinema);
    // One shot for every feed: square, portrait and vertical crops.
    Settings social = with({"1:1", "4:5", "9:16"}, -1, false);
    social.actionSafe = false;
    list.emplace_back("Social", social);
    Settings clear = defaults();
    list.emplace_back("Clear", clear);
    return list;
}

} // namespace fla::safezones
