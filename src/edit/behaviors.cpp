#include "behaviors.h"

#include "../data/symbol_instance.h"

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
