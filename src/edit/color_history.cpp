#include "color_history.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace fla {

bool ColorHistory::add(const Color& color)
{
    if (!_colors.empty() && _colors.front() == color)
        return false;
    const auto found = std::find(_colors.begin(), _colors.end(), color);
    if (found != _colors.end())
        _colors.erase(found);
    _colors.insert(_colors.begin(), color);
    if (_colors.size() > _limit)
        _colors.resize(_limit);
    return true;
}

void ColorHistory::setLimit(size_t limit)
{
    _limit = limit == 0 ? 1 : limit;
    if (_colors.size() > _limit)
        _colors.resize(_limit);
}

std::string ColorHistory::toText() const
{
    std::string text;
    for (const Color& color : _colors)
    {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", color[0], color[1], color[2], color[3]);
        if (!text.empty())
            text += ' ';
        text += hex;
    }
    return text;
}

ColorHistory ColorHistory::fromText(const std::string& text, size_t limit)
{
    ColorHistory history(limit);
    std::istringstream words(text);
    std::string word;
    std::vector<Color> read;
    while (words >> word)
    {
        if (word.size() != 7 && word.size() != 9)
            continue;
        if (word[0] != '#' || word.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
            continue;
        Color color = {0, 0, 0, 255};
        for (size_t i = 0; i * 2 + 1 < word.size(); ++i)
            color[i] = static_cast<uint8_t>(std::stoi(word.substr(1 + i * 2, 2), nullptr, 16));
        if (std::find(read.begin(), read.end(), color) == read.end())
            read.push_back(color);
    }
    // Newest first in the text: added oldest first, so each goes in front.
    for (auto it = read.rbegin(); it != read.rend(); ++it)
        history.add(*it);
    return history;
}

Hsv hsvOf(uint8_t red, uint8_t green, uint8_t blue)
{
    const double r = red / 255.0;
    const double g = green / 255.0;
    const double b = blue / 255.0;
    const double top = std::max({r, g, b});
    const double bottom = std::min({r, g, b});
    const double range = top - bottom;
    Hsv hsv;
    hsv.v = top;
    hsv.s = top > 0.0 ? range / top : 0.0;
    if (range > 0.0)
    {
        double h;
        if (top == r)
            h = std::fmod((g - b) / range, 6.0);
        else if (top == g)
            h = (b - r) / range + 2.0;
        else
            h = (r - g) / range + 4.0;
        h *= 60.0;
        if (h < 0.0)
            h += 360.0;
        hsv.h = h >= 360.0 ? 0.0 : h;
    }
    return hsv;
}

std::array<uint8_t, 4> rgbOf(const Hsv& hsv, uint8_t alpha)
{
    double h = std::fmod(hsv.h, 360.0);
    if (h < 0.0)
        h += 360.0;
    const double s = std::clamp(hsv.s, 0.0, 1.0);
    const double v = std::clamp(hsv.v, 0.0, 1.0);
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (static_cast<int>(h / 60.0))
    {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    const auto byte = [](double value) {
        return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
    };
    return {byte(r + m), byte(g + m), byte(b + m), alpha};
}

} // namespace fla
