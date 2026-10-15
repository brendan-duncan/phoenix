#include "text_animators.h"

#include "../data/static_text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>

namespace fla::textanim {

const char* const kAttribute = "phoenixTextAnimators";

namespace {

using behaviors::Applied;
using behaviors::Definition;
using behaviors::ParamSpec;

constexpr double kPi = 3.14159265358979323846;

ParamSpec number(const char* name, const char* label, double value, double min, double max, double step)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.value = value;
    spec.min = min;
    spec.max = max;
    spec.step = step;
    char text[32];
    std::snprintf(text, sizeof(text), "%.10g", value);
    spec.text = text;
    return spec;
}

ParamSpec choice(const char* name, const char* label, std::vector<std::string> choices)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "choice";
    spec.choices = std::move(choices);
    spec.text = spec.choices.empty() ? std::string() : spec.choices.front();
    return spec;
}

ParamSpec checkbox(const char* name, const char* label, bool value)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "checkbox";
    spec.value = value ? 1.0 : 0.0;
    spec.text = value ? "true" : "false";
    return spec;
}

ParamSpec textParam(const char* name, const char* label, const char* value)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "text";
    spec.text = value;
    return spec;
}

ParamSpec seed()
{
    ParamSpec spec = number("seed", "Seed", 1, 0, 9999, 1);
    spec.type = "seed";
    return spec;
}

Definition definition(const char* id, const char* name, const char* description, std::vector<ParamSpec> params)
{
    Definition made;
    made.id = id;
    made.name = name;
    made.description = description;
    made.builtIn = true;
    made.params = std::move(params);
    return made;
}

double param(const Applied& applied, const char* name, int frame, double fallback)
{
    const std::string text = applied.valueAt(name, frame, std::string());
    if (text.empty())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : value;
}

std::string text(const Applied& applied, const char* name, int frame, const std::string& fallback)
{
    const std::string value = applied.valueAt(name, frame, std::string());
    return value.empty() ? fallback : value;
}

bool flag(const Applied& applied, const char* name, int frame, bool fallback)
{
    const std::string value = applied.valueAt(name, frame, std::string());
    return value.empty() ? fallback : value == "true" || value == "1";
}

double hash(int seedValue, int64_t n)
{
    uint32_t h = static_cast<uint32_t>((static_cast<uint32_t>(seedValue) ^ 0x9e3779b9u) * 0x85ebca6bu) ^
        static_cast<uint32_t>(static_cast<uint64_t>(n) * 0xc2b2ae35u);
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    h ^= h >> 16;
    return h / 4294967296.0;
}

double clamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

double easeOutCubic(double t)
{
    t = clamp01(t);
    return 1.0 - std::pow(1.0 - t, 3.0);
}

/// Past the end and back: \a overshoot is how far past, as a part of the
/// whole.
double backOut(double t, double overshoot)
{
    t = clamp01(t);
    const double s = 1.70158 * overshoot / 0.1;
    const double u = t - 1.0;
    return u * u * ((s + 1.0) * u + s) + 1.0;
}

/// Each letter's place in a range selector's order, from 0 to 1.
double placeOf(const Letter& letter, const std::string& unit)
{
    if (unit == "Words")
        return (letter.word + 0.5) / std::max(1, letter.words);
    if (unit == "Lines")
        return (letter.line + 0.5) / std::max(1, letter.lines);
    return (letter.index + 0.5) / std::max(1, letter.count);
}

} // namespace

const std::vector<Definition>& definitions()
{
    static const std::vector<Definition> list = {
        definition("typewriter", "Typewriter", "Types the text out a letter at a time from the start of its keyframes.",
            {number("rate", "Letters per second", 15, 0.1, 500, 0.5), number("delay", "Wait first (seconds)", 0, 0, 600, 0.05)}),
        definition("cascade", "Cascade In", "Each letter slides in from one side, fading up, a little after the one before.",
            {choice("from", "From", {"Above", "Below", "Left", "Right"}), number("distance", "Distance (pixels)", 40, 0, 2000, 1),
                number("duration", "Each letter takes (seconds)", 0.4, 0.01, 10, 0.01),
                number("stagger", "Between letters (seconds)", 0.05, 0, 5, 0.01), number("delay", "Wait first (seconds)", 0, 0, 600, 0.05),
                checkbox("fade", "Fade in", true)}),
        definition("bounce", "Bounce In", "Each letter grows from nothing, past its size and back, a little after the one before.",
            {number("duration", "Each letter takes (seconds)", 0.5, 0.01, 10, 0.01),
                number("stagger", "Between letters (seconds)", 0.05, 0, 5, 0.01), number("overshoot", "Overshoot (percent)", 30, 0, 200, 1),
                number("delay", "Wait first (seconds)", 0, 0, 600, 0.05)}),
        definition("fadeword", "Fade by Word", "The words fade in one after another.",
            {number("duration", "Each word takes (seconds)", 0.5, 0.01, 10, 0.01),
                number("stagger", "Between words (seconds)", 0.25, 0, 10, 0.01), number("delay", "Wait first (seconds)", 0, 0, 600, 0.05)}),
        definition("scramble", "Scramble", "Letters flicker through random characters until each settles on its own, left to right.",
            {number("rate", "Letters settled per second", 10, 0.1, 500, 0.5), number("delay", "Wait first (seconds)", 0, 0, 600, 0.05),
                number("changes", "Flickers per second", 12, 1, 60, 1), choice("set", "Random characters", {"Letters", "Digits", "Symbols"}),
                seed()}),
        definition("wave", "Wave", "A wave runs through the letters, lifting and tilting them.",
            {number("height", "Height (pixels)", 8, -500, 500, 0.5), number("length", "Letters per wave", 8, 1, 200, 0.5),
                number("speed", "Waves per second", 1, -20, 20, 0.05), number("tilt", "Tilt (degrees)", 0, -90, 90, 1)}),
        definition("range", "Animate Letters",
            "Changes the letters a range covers: where they are, their turn, size, opacity, spacing, color and blur. Key "
            "the Offset to sweep the range across the text, so the change passes from letter to letter.",
            {choice("unit", "By", {"Letters", "Words", "Lines"}),
                choice("shape", "Shape", {"Square", "Ramp up", "Ramp down", "Triangle", "Round", "Random"}),
                number("start", "Start (percent)", 0, 0, 100, 0.5), number("end", "End (percent)", 100, 0, 100, 0.5),
                number("offset", "Offset (percent)", 0, -200, 200, 0.5), number("moveX", "Move across (pixels)", 0, -2000, 2000, 1),
                number("moveY", "Move down (pixels)", -20, -2000, 2000, 1), number("turn", "Turn (degrees)", 0, -720, 720, 1),
                number("scale", "Size (percent)", 100, 0, 1000, 1), number("opacity", "Opacity (percent)", 100, 0, 100, 1),
                number("tracking", "Spacing (pixels)", 0, -200, 500, 0.5), textParam("color", "Color (#RRGGBB)", "#FFC000"),
                number("colorAmount", "Color amount (percent)", 0, 0, 100, 1), number("blur", "Blur (pixels)", 0, 0, 100, 0.5),
                seed()}),
    };
    return list;
}

const Definition* find(const std::string& id)
{
    for (const Definition& each : definitions())
        if (each.id == id)
            return &each;
    return nullptr;
}

Applied defaults(const std::string& id, int seedValue)
{
    Applied applied;
    applied.type = id;
    if (const Definition* each = find(id))
        for (const ParamSpec& spec : each->params)
            applied.params[spec.name] = spec.type == "seed" ? std::to_string(seedValue) : spec.text;
    return applied;
}

std::vector<Applied> of(const StaticText& field)
{
    return behaviors::listOf(field, kAttribute);
}

bool any(const StaticText& field)
{
    for (const Applied& applied : of(field))
        if (applied.enabled && find(applied.type))
            return true;
    return false;
}

std::vector<Change> apply(const std::vector<Applied>& list, const std::vector<Letter>& letters, int frame, int start,
    double frameRate)
{
    std::vector<Change> changes(letters.size());
    const double rate = frameRate > 0.0 ? frameRate : 24.0;
    const double time = (frame - start) / rate;
    for (const Applied& applied : list)
    {
        if (!applied.enabled)
            continue;
        const std::string& type = applied.type;
        const double delay = param(applied, "delay", frame, 0.0);
        const double t = time - delay;
        if (type == "typewriter")
        {
            const double perSecond = std::max(0.1, param(applied, "rate", frame, 15.0));
            for (size_t i = 0; i < letters.size(); ++i)
                if (t < letters[i].index / perSecond)
                    changes[i].alpha = 0.0;
        }
        else if (type == "cascade")
        {
            const std::string from = text(applied, "from", frame, "Above");
            const double distance = param(applied, "distance", frame, 40.0);
            const double duration = std::max(0.01, param(applied, "duration", frame, 0.4));
            const double stagger = param(applied, "stagger", frame, 0.05);
            const bool fade = flag(applied, "fade", frame, true);
            for (size_t i = 0; i < letters.size(); ++i)
            {
                const double u = easeOutCubic((t - letters[i].index * stagger) / duration);
                const double away = (1.0 - u) * distance;
                if (from == "Below")
                    changes[i].y += away;
                else if (from == "Left")
                    changes[i].x -= away;
                else if (from == "Right")
                    changes[i].x += away;
                else
                    changes[i].y -= away;
                if (fade)
                    changes[i].alpha *= u;
            }
        }
        else if (type == "bounce")
        {
            const double duration = std::max(0.01, param(applied, "duration", frame, 0.5));
            const double stagger = param(applied, "stagger", frame, 0.05);
            const double overshoot = param(applied, "overshoot", frame, 30.0) / 100.0;
            for (size_t i = 0; i < letters.size(); ++i)
            {
                const double u = (t - letters[i].index * stagger) / duration;
                changes[i].scale *= u <= 0.0 ? 0.0 : std::max(0.0, backOut(u, overshoot));
            }
        }
        else if (type == "fadeword")
        {
            const double duration = std::max(0.01, param(applied, "duration", frame, 0.5));
            const double stagger = param(applied, "stagger", frame, 0.25);
            for (size_t i = 0; i < letters.size(); ++i)
                changes[i].alpha *= clamp01((t - letters[i].word * stagger) / duration);
        }
        else if (type == "scramble")
        {
            const double perSecond = std::max(0.1, param(applied, "rate", frame, 10.0));
            const double flickers = std::max(1.0, param(applied, "changes", frame, 12.0));
            const std::string set = text(applied, "set", frame, "Letters");
            const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0));
            static const std::string letterSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            static const std::string digitSet = "0123456789";
            static const std::string symbolSet = "#$%&*+=?@<>/\\|~^!";
            const std::string& pool = set == "Digits" ? digitSet : set == "Symbols" ? symbolSet : letterSet;
            const int64_t tick = static_cast<int64_t>(std::floor(std::max(0.0, time) * flickers));
            for (size_t i = 0; i < letters.size(); ++i)
            {
                const unsigned c = letters[i].character;
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || t >= letters[i].index / perSecond)
                    continue;
                const double pick = hash(seedValue, tick * 7919 + letters[i].index);
                changes[i].character = static_cast<unsigned char>(pool[static_cast<size_t>(pick * pool.size()) % pool.size()]);
            }
        }
        else if (type == "wave")
        {
            const double height = param(applied, "height", frame, 8.0);
            const double length = std::max(1.0, param(applied, "length", frame, 8.0));
            const double speed = param(applied, "speed", frame, 1.0);
            const double tilt = param(applied, "tilt", frame, 0.0);
            for (size_t i = 0; i < letters.size(); ++i)
            {
                const double phase = 2.0 * kPi * (letters[i].index / length - time * speed);
                changes[i].y -= std::sin(phase) * height;
                changes[i].rotation += std::cos(phase) * tilt;
            }
        }
        else if (type == "range")
        {
            const std::string unit = text(applied, "unit", frame, "Letters");
            const std::string shape = text(applied, "shape", frame, "Square");
            const double offset = param(applied, "offset", frame, 0.0);
            const double a = (param(applied, "start", frame, 0.0) + offset) / 100.0;
            const double b = (param(applied, "end", frame, 100.0) + offset) / 100.0;
            const double moveX = param(applied, "moveX", frame, 0.0);
            const double moveY = param(applied, "moveY", frame, -20.0);
            const double turn = param(applied, "turn", frame, 0.0);
            const double scale = param(applied, "scale", frame, 100.0) / 100.0;
            const double opacity = param(applied, "opacity", frame, 100.0) / 100.0;
            const double tracking = param(applied, "tracking", frame, 0.0);
            const double colorAmount = std::clamp(param(applied, "colorAmount", frame, 0.0) / 100.0, 0.0, 1.0);
            const double blur = std::max(0.0, param(applied, "blur", frame, 0.0));
            std::string colorText = text(applied, "color", frame, "#FFC000");
            if (!colorText.empty() && colorText[0] == '#')
                colorText = colorText.substr(1);
            const unsigned color = static_cast<unsigned>(std::strtoul(colorText.c_str(), nullptr, 16)) & 0xFFFFFFu;
            const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0));
            // Spacing pushes each letter on by the spacing of those before it.
            double carried = 0.0;
            // In random order, each letter takes another's place.
            std::vector<double> places(letters.size());
            for (size_t i = 0; i < letters.size(); ++i)
                places[i] = placeOf(letters[i], unit);
            if (shape == "Random" && !letters.empty())
            {
                std::vector<size_t> order(letters.size());
                std::iota(order.begin(), order.end(), size_t(0));
                std::sort(order.begin(), order.end(), [&](size_t x, size_t y) {
                    return hash(seedValue, letters[x].index) < hash(seedValue, letters[y].index);
                });
                for (size_t rank = 0; rank < order.size(); ++rank)
                    places[order[rank]] = (rank + 0.5) / order.size();
            }
            for (size_t i = 0; i < letters.size(); ++i)
            {
                const double u = places[i];
                double s = 0.0;
                if (b > a && u >= a && u < b)
                {
                    const double x = (u - a) / (b - a);
                    if (shape == "Ramp up")
                        s = x;
                    else if (shape == "Ramp down")
                        s = 1.0 - x;
                    else if (shape == "Triangle")
                        s = 1.0 - std::fabs(2.0 * x - 1.0);
                    else if (shape == "Round")
                        s = std::sqrt(std::max(0.0, 1.0 - (2.0 * x - 1.0) * (2.0 * x - 1.0)));
                    else
                        s = 1.0;
                }
                changes[i].x += moveX * s;
                changes[i].y += moveY * s;
                changes[i].rotation += turn * s;
                changes[i].scale *= 1.0 + (scale - 1.0) * s;
                changes[i].alpha *= 1.0 + (opacity - 1.0) * s;
                changes[i].x += carried;
                carried += tracking * s;
                if (colorAmount * s > changes[i].tint)
                {
                    changes[i].tint = colorAmount * s;
                    changes[i].tintColor = color;
                }
                changes[i].blur += blur * s;
            }
        }
    }
    return changes;
}

} // namespace fla::textanim
