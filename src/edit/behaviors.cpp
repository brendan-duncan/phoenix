#include "behaviors.h"

#include "motion_object.h"

#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla::behaviors {

namespace {

constexpr double kPi = 3.14159265358979323846;
const char* const kAttribute = "phoenixBehaviors";

/// The characters the list's text gives meaning to, written as %XX.
std::string escape(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '%' || c == '|' || c == '&' || c == '=' || c == '?' || c == '!' || c == '\n' || c == '\r')
        {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            out += hex;
        }
        else
            out += c;
    }
    return out;
}

std::string unescape(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char by)
{
    std::vector<std::string> parts;
    size_t from = 0;
    for (;;)
    {
        const size_t at = text.find(by, from);
        parts.push_back(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos)
            break;
        from = at + 1;
    }
    return parts;
}

/// A key as its text keeps it: its frame, its value, and its curve's
/// handles (what follows a ~ after the value), when it has them.
struct RawKey
{
    int frame = 0;
    std::string value;
    std::string handles;
};

std::vector<RawKey> rawKeys(const std::string& text)
{
    std::vector<RawKey> list;
    if (text.empty() || text[0] != '@')
        return list;
    for (const std::string& key : split(text.substr(1), ';'))
    {
        const size_t equals = key.find('=');
        if (equals == std::string::npos)
            continue;
        RawKey raw;
        raw.frame = std::atoi(key.substr(0, equals).c_str());
        raw.value = key.substr(equals + 1);
        const size_t tilde = raw.value.find('~');
        if (tilde != std::string::npos)
        {
            raw.handles = raw.value.substr(tilde + 1);
            raw.value.erase(tilde);
        }
        list.push_back(raw);
    }
    std::stable_sort(list.begin(), list.end(), [](const RawKey& a, const RawKey& b) { return a.frame < b.frame; });
    return list;
}

std::string joined(const std::vector<RawKey>& list)
{
    std::string text = "@";
    for (size_t i = 0; i < list.size(); ++i)
    {
        text += (i ? ";" : "") + std::to_string(list[i].frame) + "=" + list[i].value;
        if (!list[i].handles.empty())
            text += "~" + list[i].handles;
    }
    return text;
}

/// A key's handles, from the key: frames and value before it, then after.
bool handlesOf(const std::string& text, double out[4])
{
    const std::vector<std::string> parts = split(text, ',');
    if (text.empty() || parts.size() != 4)
        return false;
    for (int i = 0; i < 4; ++i)
        out[i] = std::strtod(parts[static_cast<size_t>(i)].c_str(), nullptr);
    return true;
}

bool numberOf(const std::string& text, double& value)
{
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && !*end;
}

std::string numberText(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.10g", value);
    return text;
}

} // namespace

double Applied::number(const std::string& name, double fallback) const
{
    const auto found = params.find(name);
    if (found == params.end() || found->second.empty())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(found->second.c_str(), &end);
    return end == found->second.c_str() ? fallback : value;
}

bool Applied::operator==(const Applied& other) const
{
    return type == other.type && enabled == other.enabled && params == other.params;
}

bool Applied::keyed(const std::string& name) const
{
    const auto found = params.find(name);
    return found != params.end() && !found->second.empty() && found->second[0] == '@';
}

std::vector<std::pair<int, std::string>> Applied::keys(const std::string& name) const
{
    std::vector<std::pair<int, std::string>> list;
    if (!keyed(name))
        return list;
    for (const RawKey& key : rawKeys(params.at(name)))
        list.emplace_back(key.frame, key.value);
    return list;
}

std::string Applied::valueAt(const std::string& name, int frame, const std::string& fallback) const
{
    const auto found = params.find(name);
    if (found == params.end())
        return fallback;
    if (!keyed(name))
        return found->second;
    const std::vector<RawKey> list = rawKeys(found->second);
    if (list.empty())
        return fallback;
    if (frame <= list.front().frame)
        return list.front().value;
    if (frame >= list.back().frame)
        return list.back().value;
    for (size_t i = 0; i + 1 < list.size(); ++i)
    {
        const RawKey& a = list[i];
        const RawKey& b = list[i + 1];
        if (frame < a.frame || frame >= b.frame)
            continue;
        double va = 0.0;
        double vb = 0.0;
        // Numbers in a line between the keys; anything else holds.
        if (!numberOf(a.value, va) || !numberOf(b.value, vb) || b.frame == a.frame)
            return a.value;
        // Shaped as a curve: along the Bezier the two keys' handles make.
        double out[4] = {0.0, 0.0, 0.0, 0.0};
        double in[4] = {0.0, 0.0, 0.0, 0.0};
        const bool bent = handlesOf(a.handles, out);
        if (handlesOf(b.handles, in) || bent)
        {
            motion::PropertyKey from = motion::PropertyKey::at(a.frame, va);
            from.nextTime = out[2];
            from.nextValue = va + out[3];
            motion::PropertyKey to = motion::PropertyKey::at(b.frame, vb);
            to.previousTime = in[0];
            to.previousValue = vb + in[1];
            return numberText(motion::valueOf({from, to}, frame));
        }
        return numberText(va + (vb - va) * (frame - a.frame) / static_cast<double>(b.frame - a.frame));
    }
    return list.back().value;
}

void Applied::setKey(const std::string& name, int frame, const std::string& value)
{
    // The other keys keep their handles, and so does this one: they are
    // kept from its value.
    std::vector<RawKey> list = keyed(name) ? rawKeys(params.at(name)) : std::vector<RawKey>();
    bool replaced = false;
    for (RawKey& key : list)
        if (key.frame == frame)
        {
            key.value = value;
            replaced = true;
        }
    if (!replaced)
        list.push_back({frame, value, std::string()});
    std::stable_sort(list.begin(), list.end(), [](const RawKey& a, const RawKey& b) { return a.frame < b.frame; });
    params[name] = joined(list);
}

void Applied::removeKey(const std::string& name, int frame, const std::string& value)
{
    std::vector<RawKey> list = keyed(name) ? rawKeys(params.at(name)) : std::vector<RawKey>();
    list.erase(std::remove_if(list.begin(), list.end(), [frame](const RawKey& key) { return key.frame == frame; }),
        list.end());
    if (list.empty())
    {
        params[name] = value;
        return;
    }
    params[name] = joined(list);
}

std::vector<motion::PropertyKey> Applied::curve(const std::string& name) const
{
    std::vector<motion::PropertyKey> curve;
    if (!keyed(name))
        return curve;
    for (const RawKey& raw : rawKeys(params.at(name)))
    {
        double value = 0.0;
        if (!numberOf(raw.value, value))
            continue;
        motion::PropertyKey key = motion::PropertyKey::at(raw.frame, value);
        double handles[4];
        if (handlesOf(raw.handles, handles))
        {
            key.previousTime = handles[0];
            key.previousValue = value + handles[1];
            key.nextTime = handles[2];
            key.nextValue = value + handles[3];
        }
        curve.push_back(key);
    }
    return curve;
}

void Applied::setCurve(const std::string& name, const std::vector<motion::PropertyKey>& keys)
{
    std::vector<RawKey> list;
    for (const motion::PropertyKey& key : keys)
    {
        RawKey raw;
        raw.frame = static_cast<int>(std::lround(key.time));
        raw.value = numberText(key.value);
        // Handles on the key itself are a straight line: none kept.
        const double handles[4] = {key.previousTime, key.previousValue - key.value, key.nextTime,
            key.nextValue - key.value};
        if (std::any_of(std::begin(handles), std::end(handles), [](double h) { return std::fabs(h) > 1.0e-9; }))
        {
            char text[128];
            std::snprintf(text, sizeof(text), "%.6g,%.6g,%.6g,%.6g", handles[0], handles[1], handles[2], handles[3]);
            raw.handles = text;
        }
        list.erase(std::remove_if(list.begin(), list.end(), [&raw](const RawKey& k) { return k.frame == raw.frame; }),
            list.end());
        list.push_back(raw);
    }
    std::stable_sort(list.begin(), list.end(), [](const RawKey& a, const RawKey& b) { return a.frame < b.frame; });
    if (!list.empty())
        params[name] = joined(list);
}

std::string encode(const std::vector<Applied>& list)
{
    std::string out;
    for (const Applied& applied : list)
    {
        if (!out.empty())
            out += '|';
        out += escape(applied.type);
        if (!applied.enabled)
            out += '!';
        bool first = true;
        for (const auto& [name, value] : applied.params)
        {
            out += first ? '?' : '&';
            first = false;
            out += escape(name) + "=" + escape(value);
        }
    }
    return out;
}

std::vector<Applied> decode(const std::string& text)
{
    std::vector<Applied> list;
    if (text.empty())
        return list;
    for (const std::string& entry : split(text, '|'))
    {
        if (entry.empty())
            continue;
        Applied applied;
        const size_t query = entry.find('?');
        std::string head = entry.substr(0, query);
        if (!head.empty() && head.back() == '!')
        {
            applied.enabled = false;
            head.pop_back();
        }
        applied.type = unescape(head);
        if (query != std::string::npos)
        {
            for (const std::string& pair : split(entry.substr(query + 1), '&'))
            {
                const size_t equals = pair.find('=');
                if (equals == std::string::npos)
                    continue;
                applied.params[unescape(pair.substr(0, equals))] = unescape(pair.substr(equals + 1));
            }
        }
        if (!applied.type.empty())
            list.push_back(applied);
    }
    return list;
}

std::vector<Applied> listOf(const DOMElement& element, const std::string& attribute)
{
    for (const auto& each : element.extraAttributes)
        if (each.first == attribute)
            return decode(each.second);
    return {};
}

std::vector<std::pair<std::string, std::string>> withList(const DOMElement& element, const std::string& attribute,
    const std::vector<Applied>& list)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& each : element.extraAttributes)
        if (each.first != attribute)
            attributes.push_back(each);
    if (!list.empty())
        attributes.emplace_back(attribute, encode(list));
    return attributes;
}

std::vector<Applied> of(const SymbolInstance& instance)
{
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first == kAttribute)
            return decode(attribute.second);
    return {};
}

bool any(const SymbolInstance& instance)
{
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first == kAttribute)
        {
            for (const Applied& applied : decode(attribute.second))
                if (applied.enabled)
                    return true;
            return false;
        }
    return false;
}

std::vector<std::pair<std::string, std::string>> withBehaviors(const SymbolInstance& instance,
    const std::vector<Applied>& list)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first != kAttribute)
            attributes.push_back(attribute);
    if (!list.empty())
        attributes.emplace_back(kAttribute, encode(list));
    return attributes;
}

Keyed keyedOf(const Transform& t, const Point& pivot, const ColorTransform& color)
{
    Keyed keyed;
    keyed.x = t.m11 * pivot.x + t.m21 * pivot.y + t.tx;
    keyed.y = t.m12 * pivot.x + t.m22 * pivot.y + t.ty;
    keyed.rotation = std::atan2(t.m12, t.m11) * 180.0 / kPi;
    keyed.scaleX = std::hypot(t.m11, t.m12);
    const double det = t.m11 * t.m22 - t.m12 * t.m21;
    keyed.scaleY = keyed.scaleX > 0.0 ? det / keyed.scaleX : std::hypot(t.m21, t.m22);
    keyed.alpha = color.alphaMultiplier;
    return keyed;
}

void Delta::add(const Delta& other)
{
    x += other.x;
    y += other.y;
    rotation += other.rotation;
    scaleX *= other.scaleX;
    scaleY *= other.scaleY;
    alpha *= other.alpha;
    frame += other.frame;
    if (other.hold >= 0)
        hold = other.hold;
    if (other.tint > 0.0)
    {
        tint = other.tint;
        for (int c = 0; c < 3; ++c)
            tintColor[c] = other.tintColor[c];
    }
}

Transform apply(const Transform& keyed, const Point& pivot, const Delta& delta)
{
    if (delta.none())
        return keyed;
    // keyed, then in the instance's own space: to the pivot, scale, turn,
    // back; then the move on the stage.
    const double r = delta.rotation * kPi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    // R * S, as m11 m12 m21 m22 with x' = m11 x + m21 y.
    const double a = c * delta.scaleX;
    const double b = s * delta.scaleX;
    const double cc = -s * delta.scaleY;
    const double d = c * delta.scaleY;
    // About the pivot: x' = M (x - p) + p.
    const Transform local(a, b, cc, d, pivot.x - (a * pivot.x + cc * pivot.y), pivot.y - (b * pivot.x + d * pivot.y));
    Transform out = keyed;
    out.concatenate(local);
    out.tx += delta.x;
    out.ty += delta.y;
    return out;
}

} // namespace fla::behaviors
