#include "templates.h"

#include "../data/bitmap_instance.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace fla::templates {

const char* const kAttribute = "phoenixTemplate";

namespace {

void collect(const Symbol& symbol, std::vector<Field>& fields, std::set<std::string>& names,
    std::set<const Symbol*>& seen);

void collectElement(const Element* element, std::vector<Field>& fields, std::set<std::string>& names,
    std::set<const Symbol*>& seen, std::vector<const Symbol*>& nested)
{
    if (!element)
        return;
    switch (element->elementType())
    {
    case Element::Type::Group:
        for (const Element* member : static_cast<const Group*>(element)->members)
            collectElement(member, fields, names, seen, nested);
        break;
    case Element::Type::StaticText:
    {
        const auto* text = static_cast<const StaticText*>(element);
        if (text->textType == StaticText::TextType::Static || text->instanceName.empty() ||
            !names.insert(text->instanceName).second)
            break;
        Field field;
        field.name = text->instanceName;
        for (const TextRun& run : text->runs)
            field.value += run.text;
        fields.push_back(field);
        break;
    }
    case Element::Type::SymbolInstance:
    {
        const auto* instance = static_cast<const SymbolInstance*>(element);
        if (instance->symbolType == SymbolType::MovieClip && !instance->name.empty() && names.insert(instance->name).second)
        {
            Field field;
            field.name = instance->name;
            field.kind = instance->symbol ? clipKind(*instance->symbol, &field.frames, &field.value) : Field::Kind::Color;
            fields.push_back(field);
        }
        if (instance->symbol)
            nested.push_back(instance->symbol);
        break;
    }
    default:
        break;
    }
}

void collect(const Symbol& symbol, std::vector<Field>& fields, std::set<std::string>& names,
    std::set<const Symbol*>& seen)
{
    if (!seen.insert(&symbol).second)
        return;
    std::vector<const Symbol*> nested;
    for (const Timeline* timeline : symbol.timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                    collectElement(element, fields, names, seen, nested);
    for (const Symbol* inner : nested)
        collect(*inner, fields, names, seen);
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '%' || c == '&' || c == '=' || static_cast<unsigned char>(c) < 0x20)
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

std::string unescaped(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size() && hexDigit(text[i + 1]) >= 0 && hexDigit(text[i + 2]) >= 0)
        {
            out += static_cast<char>(hexDigit(text[i + 1]) * 16 + hexDigit(text[i + 2]));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

} // namespace

Field::Kind clipKind(const Symbol& symbol, int* frames, std::string* bitmap)
{
    int length = 1;
    int elements = 0;
    const BitmapInstance* only = nullptr;
    for (const Timeline* timeline : symbol.timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                length = std::max(length, frame->index + std::max(frame->duration, 1));
                for (const Element* element : frame->elements)
                {
                    if (!element)
                        continue;
                    ++elements;
                    if (element->elementType() == Element::Type::BitmapInstance)
                        only = static_cast<const BitmapInstance*>(element);
                }
            }
    if (frames)
        *frames = length;
    if (length > 1)
        return Field::Kind::Frame;
    if (elements == 1 && only)
    {
        if (bitmap)
            *bitmap = only->libraryItemName;
        return Field::Kind::Image;
    }
    return Field::Kind::Color;
}

std::vector<Field> fieldsOf(const Symbol& symbol)
{
    std::vector<Field> fields;
    std::set<std::string> names;
    std::set<const Symbol*> seen;
    collect(symbol, fields, names, seen);
    return fields;
}

Values valuesOf(const SymbolInstance& instance)
{
    for (const auto& [name, value] : instance.extraAttributes)
        if (name == kAttribute)
            return decode(value);
    return Values();
}

std::vector<std::pair<std::string, std::string>> withValues(const SymbolInstance& instance, const Values& values)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& each : instance.extraAttributes)
        if (each.first != kAttribute)
            out.push_back(each);
    if (!values.empty())
        out.emplace_back(kAttribute, encode(values));
    return out;
}

bool any(const SymbolInstance& instance)
{
    for (const auto& each : instance.extraAttributes)
        if (each.first == kAttribute && !each.second.empty())
            return true;
    return false;
}

std::string encode(const Values& values)
{
    std::string out;
    for (const auto& [name, value] : values)
    {
        if (!out.empty())
            out += '&';
        out += escaped(name) + "=" + escaped(value);
    }
    return out;
}

Values decode(const std::string& text)
{
    Values values;
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('&', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string pair = text.substr(at, end - at);
        const size_t equals = pair.find('=');
        if (equals != std::string::npos && equals > 0)
            values[unescaped(pair.substr(0, equals))] = unescaped(pair.substr(equals + 1));
        at = end + 1;
    }
    return values;
}

bool colorOf(const std::string& value, unsigned char rgb[3])
{
    std::string hex = value;
    if (!hex.empty() && hex[0] == '#')
        hex = hex.substr(1);
    if (hex.size() != 6)
        return false;
    for (const char c : hex)
        if (hexDigit(c) < 0)
            return false;
    for (int i = 0; i < 3; ++i)
        rgb[i] = static_cast<unsigned char>(hexDigit(hex[i * 2]) * 16 + hexDigit(hex[i * 2 + 1]));
    return true;
}

bool frameOf(const std::string& value, int& frame)
{
    if (value.empty() || value.size() > 6)
        return false;
    int n = 0;
    for (const char c : value)
    {
        if (c < '0' || c > '9')
            return false;
        n = n * 10 + (c - '0');
    }
    if (n < 1)
        return false;
    frame = n;
    return true;
}

std::vector<Values> rowsOfCsv(const std::string& text)
{
    // Cells, row by row.
    std::vector<std::vector<std::string>> rows(1);
    std::string cell;
    bool quoted = false;
    bool any = false;
    const auto endCell = [&]() {
        rows.back().push_back(cell);
        cell.clear();
    };
    size_t i = 0;
    // A byte order mark is not part of the first name.
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        i = 3;
    for (; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quoted)
        {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"')
            {
                cell += '"';
                ++i;
            }
            else if (c == '"')
                quoted = false;
            else
                cell += c;
            continue;
        }
        if (c == '"')
        {
            quoted = true;
            any = true;
        }
        else if (c == ',')
        {
            endCell();
            any = true;
        }
        else if (c == '\r' || c == '\n')
        {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            if (any || !cell.empty())
                endCell();
            if (!rows.back().empty())
                rows.emplace_back();
            any = false;
        }
        else
        {
            cell += c;
            any = true;
        }
    }
    if (any || !cell.empty())
        endCell();
    if (rows.back().empty())
        rows.pop_back();

    std::vector<Values> out;
    if (rows.empty())
        return out;
    const std::vector<std::string>& names = rows.front();
    for (size_t r = 1; r < rows.size(); ++r)
    {
        Values values;
        for (size_t c = 0; c < rows[r].size() && c < names.size(); ++c)
            if (!names[c].empty() && !rows[r][c].empty())
                values[names[c]] = rows[r][c];
        out.push_back(values);
    }
    return out;
}

} // namespace fla::templates
