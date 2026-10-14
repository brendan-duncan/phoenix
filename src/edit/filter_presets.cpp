#include "filter_presets.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <cstdint>

namespace fla {

namespace filterpresets {

namespace {

const char* const kTypes[] = {"blur", "dropShadow", "glow", "bevel", "gradientGlow", "gradientBevel", "adjustColor"};

std::string number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

std::string color(const uint8_t rgba[4])
{
    return std::to_string(rgba[0]) + "," + std::to_string(rgba[1]) + "," + std::to_string(rgba[2]) + "," +
        std::to_string(rgba[3]);
}

void readColor(const std::string& text, uint8_t rgba[4])
{
    std::istringstream in(text);
    std::string part;
    for (int i = 0; i < 4 && std::getline(in, part, ','); ++i)
        rgba[i] = static_cast<uint8_t>(std::atoi(part.c_str()));
}

} // namespace

std::string toText(const std::vector<Filter>& filters)
{
    std::string text;
    for (const Filter& filter : filters)
    {
        std::string line = std::string("type=") + kTypes[static_cast<int>(filter.type)];
        line += ";enabled=" + std::string(filter.enabled ? "1" : "0");
        line += ";blurX=" + number(filter.blurX) + ";blurY=" + number(filter.blurY);
        line += ";quality=" + std::to_string(filter.quality);
        line += ";angle=" + number(filter.angle) + ";distance=" + number(filter.distance);
        line += ";strength=" + number(filter.strength);
        line += ";color=" + color(filter.color);
        line += ";highlight=" + color(filter.highlightColor) + ";shadow=" + color(filter.shadowColor);
        line += ";inner=" + std::string(filter.inner ? "1" : "0");
        line += ";knockout=" + std::string(filter.knockout ? "1" : "0");
        line += ";hideObject=" + std::string(filter.hideObject ? "1" : "0");
        if (!filter.bevelType.empty())
            line += ";bevelType=" + filter.bevelType;
        line += ";brightness=" + number(filter.brightness) + ";contrast=" + number(filter.contrast);
        line += ";saturation=" + number(filter.saturation) + ";hue=" + number(filter.hue);
        if (!filter.entries.empty())
        {
            line += ";entries=";
            for (size_t i = 0; i < filter.entries.size(); ++i)
                line += (i ? "|" : "") + number(filter.entries[i].ratio) + "@" + color(filter.entries[i].color);
        }
        text += line + "\n";
    }
    return text;
}

std::vector<Filter> fromText(const std::string& text)
{
    std::vector<Filter> filters;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
    {
        std::map<std::string, std::string> values;
        std::istringstream pairs(line);
        std::string pair;
        while (std::getline(pairs, pair, ';'))
        {
            const size_t equals = pair.find('=');
            if (equals != std::string::npos)
                values[pair.substr(0, equals)] = pair.substr(equals + 1);
        }
        const auto type = values.find("type");
        if (type == values.end())
            continue;
        Filter filter;
        bool known = false;
        for (int i = 0; i < 7; ++i)
        {
            if (type->second == kTypes[i])
            {
                filter.type = static_cast<Filter::Type>(i);
                known = true;
            }
        }
        if (!known)
            continue;
        const auto get = [&values](const char* key, double fallback) {
            const auto found = values.find(key);
            return found == values.end() ? fallback : std::atof(found->second.c_str());
        };
        filter.enabled = get("enabled", 1.0) != 0.0;
        filter.blurX = get("blurX", filter.blurX);
        filter.blurY = get("blurY", filter.blurY);
        filter.quality = static_cast<int>(get("quality", filter.quality));
        filter.angle = get("angle", filter.angle);
        filter.distance = get("distance", filter.distance);
        filter.strength = get("strength", filter.strength);
        if (values.count("color"))
            readColor(values["color"], filter.color);
        if (values.count("highlight"))
            readColor(values["highlight"], filter.highlightColor);
        if (values.count("shadow"))
            readColor(values["shadow"], filter.shadowColor);
        filter.inner = get("inner", 0.0) != 0.0;
        filter.knockout = get("knockout", 0.0) != 0.0;
        filter.hideObject = get("hideObject", 0.0) != 0.0;
        if (values.count("bevelType"))
            filter.bevelType = values["bevelType"];
        filter.brightness = get("brightness", 0.0);
        filter.contrast = get("contrast", 0.0);
        filter.saturation = get("saturation", 0.0);
        filter.hue = get("hue", 0.0);
        if (values.count("entries"))
        {
            std::istringstream entries(values["entries"]);
            std::string entry;
            while (std::getline(entries, entry, '|'))
            {
                const size_t at = entry.find('@');
                if (at == std::string::npos)
                    continue;
                GradientEntry gradient;
                gradient.ratio = std::atof(entry.substr(0, at).c_str());
                readColor(entry.substr(at + 1), gradient.color);
                filter.entries.push_back(gradient);
            }
        }
        filters.push_back(filter);
    }
    return filters;
}

} // namespace filterpresets

} // namespace fla
