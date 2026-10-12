#include "text_style.h"

#include "../data/static_text.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fla {

namespace textstyle {

namespace {

const char* const kParagraph[] = {"alignment", "lineSpacing", "indent", "leftMargin", "rightMargin"};
const char* const kField[] = {"fontRenderingMode", "instanceName", "lineType", "isSelectable", "renderAsHTML", "border",
    "variableName", "maxCharacters", "orientation", "scrollable", "textType"};

std::string kept(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& key)
{
    for (const auto& [name, value] : attributes)
        if (name == key)
            return value;
    return std::string();
}

void keep(std::vector<std::pair<std::string, std::string>>& attributes, const std::string& key, const std::string& value)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(), [&key](const auto& a) { return a.first == key; }),
        attributes.end());
    // A setting at its default is left out, as Animate leaves it out. A
    // field is selectable unless it says not.
    const bool isDefault = key == "isSelectable" ? value == "true" || value.empty()
                                                 : value.empty() || value == "false" || value == "normal" || value == "0";
    if (!isDefault)
        attributes.emplace_back(key, value);
}

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

/// Everything a restyle may change about a field.
struct Snapshot
{
    std::vector<TextRun> runs;
    StaticText::TextType textType = StaticText::TextType::Static;
    std::string instanceName;
    std::string lineType;
    std::vector<std::pair<std::string, std::string>> extraAttributes;
    double width = 0.0;
    double height = 0.0;
    Rect localBounds;
    Rect bounds;

    static Snapshot of(const StaticText& text)
    {
        return Snapshot{text.runs, text.textType, text.instanceName, text.lineType, text.extraAttributes,
            text.width, text.height, text.localBounds, text.bounds};
    }
    void apply(StaticText& text) const
    {
        text.width = width;
        text.height = height;
        text.localBounds = localBounds;
        text.bounds = bounds;
        text.runs = runs;
        text.textType = textType;
        text.instanceName = instanceName;
        text.lineType = lineType;
        text.extraAttributes = extraAttributes;
    }
};

class RestyleCommand : public Command
{
public:
    explicit RestyleCommand(const std::string& name)
        : _name(name)
    {
    }
    void add(StaticText* text, Snapshot before, Snapshot after)
    {
        _changes.push_back({text, std::move(before), std::move(after)});
    }
    bool empty() const { return _changes.empty(); }
    void redo() override
    {
        for (const Change& change : _changes)
            change.after.apply(*change.text);
    }
    void undo() override
    {
        for (const Change& change : _changes)
            change.before.apply(*change.text);
    }
    std::string name() const override { return _name; }

private:
    struct Change
    {
        StaticText* text;
        Snapshot before;
        Snapshot after;
    };
    std::vector<Change> _changes;
    std::string _name;
};

} // namespace

Scope scopeOf(const std::string& key)
{
    for (const char* name : kParagraph)
        if (key == name)
            return Scope::Paragraph;
    for (const char* name : kField)
        if (key == name)
            return Scope::Field;
    return Scope::Character;
}

std::string get(const TextRun& run, const std::string& key)
{
    if (key == "face")
        return run.face;
    if (key == "size")
        return number(run.size);
    if (key == "fillColor")
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", run.fillColor[0], run.fillColor[1], run.fillColor[2],
            run.fillColor[3]);
        return buffer;
    }
    if (key == "letterSpacing")
        return number(run.letterSpacing);
    if (key == "autoKern")
        return run.autoKern ? "true" : "false";
    if (key == "alignment")
    {
        switch (run.alignment)
        {
        case TextRun::Alignment::Center: return "center";
        case TextRun::Alignment::Right: return "right";
        case TextRun::Alignment::Justify: return "justify";
        case TextRun::Alignment::Left: break;
        }
        return "left";
    }
    return kept(run.extraAttributes, key);
}

std::string get(const StaticText& text, const std::string& key)
{
    if (key == "textType")
        return text.textType == StaticText::TextType::Dynamic ? "dynamic"
            : text.textType == StaticText::TextType::Input    ? "input"
                                                              : "static";
    if (key == "instanceName")
        return text.instanceName;
    if (key == "lineType")
        return text.lineType;
    if (scopeOf(key) == Scope::Field)
        return kept(text.extraAttributes, key);
    return text.runs.empty() ? std::string() : get(text.runs.front(), key);
}

void set(TextRun& run, const std::string& key, const std::string& value)
{
    if (key == "face")
        run.face = value;
    else if (key == "size")
        run.size = std::max(1.0, std::atof(value.c_str()));
    else if (key == "fillColor")
    {
        // #rrggbb or #rrggbbaa.
        if (value.size() >= 7 && value[0] == '#')
            for (size_t i = 0; i < 4 && 1 + 2 * i + 1 < value.size(); ++i)
                run.fillColor[i] = static_cast<uint8_t>(std::strtoul(value.substr(1 + 2 * i, 2).c_str(), nullptr, 16));
    }
    else if (key == "letterSpacing")
        run.letterSpacing = std::atof(value.c_str());
    else if (key == "autoKern")
        run.autoKern = value != "false";
    else if (key == "alignment")
        run.alignment = value == "center" ? TextRun::Alignment::Center
            : value == "right"            ? TextRun::Alignment::Right
            : value == "justify"          ? TextRun::Alignment::Justify
                                          : TextRun::Alignment::Left;
    else
        keep(run.extraAttributes, key, value);
}

void set(StaticText& text, const std::string& key, const std::string& value)
{
    if (key == "textType")
        text.textType = value == "dynamic" ? StaticText::TextType::Dynamic
            : value == "input"             ? StaticText::TextType::Input
                                           : StaticText::TextType::Static;
    else if (key == "instanceName")
        text.instanceName = value;
    else if (key == "lineType")
        text.lineType = value;
    else
        keep(text.extraAttributes, key, value);
}

void setAll(StaticText& text, const std::string& key, const std::string& value)
{
    if (scopeOf(key) == Scope::Field)
    {
        set(text, key, value);
        return;
    }
    for (TextRun& run : text.runs)
        set(run, key, value);
}

CommandPtr restyle(const std::vector<StaticText*>& texts, const std::string& key, const std::string& value,
    const std::string& name, const std::function<void(StaticText&)>& measure)
{
    auto* command = new RestyleCommand(name);
    for (StaticText* text : texts)
    {
        Snapshot before = Snapshot::of(*text);
        StaticText copy(nullptr);
        before.apply(copy);
        copy.left = text->left;
        copy.top = text->top;
        copy.autoExpand = text->autoExpand;
        setAll(copy, key, value);
        if (measure)
            measure(copy);
        command->add(text, std::move(before), Snapshot::of(copy));
    }
    return CommandPtr(command);
}

} // namespace textstyle

} // namespace fla
