#include "find_replace.h"

#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/fill_style.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/linear_gradient.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fla {

namespace find {

namespace {

/// One field changed, both ways.
struct Change
{
    std::function<void()> redo;
    std::function<void()> undo;
};

class ReplaceCommand : public Command
{
public:
    ReplaceCommand(std::vector<Change> changes, std::string name)
        : _changes(std::move(changes))
        , _name(std::move(name))
    {
    }

    void redo() override
    {
        for (const Change& change : _changes)
            change.redo();
    }

    void undo() override
    {
        for (auto it = _changes.rbegin(); it != _changes.rend(); ++it)
            it->undo();
    }

    std::string name() const override { return _name; }

private:
    std::vector<Change> _changes;
    std::string _name;
};

char folded(char c, bool matchCase)
{
    return matchCase ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool wordCharacter(char c)
{
    // Bytes of a multibyte UTF-8 character count as letters.
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) & 0x80);
}

bool sameFace(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

/// Looks through one object for what \a query looks for, and when
/// \a changes is given, records the changes that replace it.
class Searcher
{
public:
    Searcher(Document& document, const Query& query, std::vector<Change>* changes)
        : _query(query)
        , _changes(changes)
    {
        if (query.kind == Kind::Symbol && document.symbolList)
        {
            const auto it = document.symbolList->symbolMap.find(query.replace);
            if (it != document.symbolList->symbolMap.end())
                _replacement = it->second;
        }
        if (query.kind == Kind::Bitmap)
            for (const Resource* resource : document.resources)
                if (resource && resource->resourceType() == Resource::Type::Bitmap &&
                    static_cast<const Bitmap*>(resource)->name == query.replace)
                    _replacementBitmap = true;
        if (query.kind == Kind::Sound)
            for (const Document::SoundInfo& sound : document.sounds)
                if (sound.name == query.replace)
                    _replacementSound = true;
    }

    bool canReplace() const
    {
        switch (_query.kind)
        {
        case Kind::Symbol: return _replacement != nullptr;
        case Kind::Bitmap: return _replacementBitmap;
        case Kind::Sound: return _replacementSound;
        default: return true;
        }
    }

    /// How many times \a frame plays the sound looked for.
    int visitFrame(Frame* frame)
    {
        if (_query.kind != Kind::Sound || !frame)
            return 0;
        int count = 0;
        for (auto& attribute : frame->extraAttributes)
        {
            if (attribute.first != "soundName" || attribute.second != _query.find)
                continue;
            ++count;
            if (_changes)
                set(attribute.second, _query.replace);
        }
        return count;
    }

    /// How many times \a element has it, inside groups too.
    int visit(Element* element)
    {
        if (!element)
            return 0;
        int count = 0;
        switch (element->elementType())
        {
        case Element::Type::StaticText:
            for (TextRun& run : static_cast<StaticText*>(element)->runs)
                count += visitRun(run);
            break;
        case Element::Type::Shape:
            if (_query.kind == Kind::Color || _query.kind == Kind::Bitmap)
            {
                Shape* shape = static_cast<Shape*>(element);
                for (FillStyle* fill : shape->fills)
                    count += visitFill(fill);
                for (StrokeStyle* stroke : shape->strokes)
                    count += stroke ? visitFill(stroke->fill) : 0;
            }
            break;
        case Element::Type::BitmapInstance:
            if (_query.kind == Kind::Bitmap)
            {
                BitmapInstance* instance = static_cast<BitmapInstance*>(element);
                if (instance->libraryItemName == _query.find)
                {
                    ++count;
                    if (_changes)
                        set(instance->libraryItemName, _query.replace);
                }
            }
            break;
        case Element::Type::Rectangle:
            if (_query.kind == Kind::Color)
            {
                RectanglePrimitive* rectangle = static_cast<RectanglePrimitive*>(element);
                count += visitFill(rectangle->fillStyle);
                count += rectangle->strokeStyle ? visitFill(rectangle->strokeStyle->fill) : 0;
            }
            break;
        case Element::Type::Oval:
            if (_query.kind == Kind::Color)
            {
                OvalPrimitive* oval = static_cast<OvalPrimitive*>(element);
                count += visitFill(oval->fillStyle);
                count += oval->strokeStyle ? visitFill(oval->strokeStyle->fill) : 0;
            }
            break;
        case Element::Type::SymbolInstance:
            if (_query.kind == Kind::Symbol)
            {
                SymbolInstance* instance = static_cast<SymbolInstance*>(element);
                if (instance->libraryItemName == _query.find)
                {
                    ++count;
                    if (_changes && _replacement)
                    {
                        const std::string beforeName = instance->libraryItemName;
                        Symbol* beforeSymbol = instance->symbol;
                        const std::string afterName = _query.replace;
                        Symbol* afterSymbol = _replacement;
                        _changes->push_back({[=]() {
                                                 instance->libraryItemName = afterName;
                                                 instance->symbol = afterSymbol;
                                             },
                            [=]() {
                                instance->libraryItemName = beforeName;
                                instance->symbol = beforeSymbol;
                            }});
                    }
                }
            }
            break;
        case Element::Type::Group:
            for (Element* member : static_cast<Group*>(element)->members)
                count += visit(member);
            break;
        default:
            break;
        }
        return count;
    }

private:
    int visitRun(TextRun& run)
    {
        switch (_query.kind)
        {
        case Kind::Text:
        {
            int count = 0;
            const std::string after = replaced(run.text, _query, count);
            if (count && _changes)
                set(run.text, after);
            return count;
        }
        case Kind::Font:
            if (_query.find.empty() || !sameFace(run.face, _query.find))
                return 0;
            if (_changes && !_query.replace.empty())
                set(run.face, _query.replace);
            return 1;
        case Kind::Color:
            return visitColor(run.fillColor);
        case Kind::Symbol:
        case Kind::Sound:
        case Kind::Bitmap:
            break;
        }
        return 0;
    }

    int visitFill(FillStyle* fill)
    {
        if (!fill)
            return 0;
        if (_query.kind == Kind::Bitmap)
        {
            auto* bitmap = dynamic_cast<BitmapFill*>(fill);
            if (!bitmap || bitmap->bitmapPath != _query.find)
                return 0;
            if (_changes)
                set(bitmap->bitmapPath, _query.replace);
            return 1;
        }
        int count = 0;
        switch (fill->type())
        {
        case FillStyle::Type::SolidColor:
            count += visitColor(static_cast<SolidColor*>(fill)->color);
            break;
        case FillStyle::Type::LinearGradient:
            for (GradientEntry& entry : static_cast<LinearGradient*>(fill)->entries)
                count += visitColor(entry.color);
            break;
        case FillStyle::Type::RadialGradient:
            for (RadialEntry& entry : static_cast<RadialGradient*>(fill)->entries)
                count += visitColor(entry.color);
            break;
        default:
            break;
        }
        return count;
    }

    int visitColor(uint8_t* color)
    {
        if (color[0] != _query.color[0] || color[1] != _query.color[1] || color[2] != _query.color[2])
            return 0;
        if (_changes)
        {
            const uint8_t before[3] = {color[0], color[1], color[2]};
            const uint8_t after[3] = {_query.replaceColor[0], _query.replaceColor[1], _query.replaceColor[2]};
            _changes->push_back({[=]() { std::copy(after, after + 3, color); },
                [=]() { std::copy(before, before + 3, color); }});
        }
        return 1;
    }

    void set(std::string& field, const std::string& after)
    {
        std::string* target = &field;
        const std::string before = field;
        _changes->push_back({[=]() { *target = after; }, [=]() { *target = before; }});
    }

    const Query& _query;
    std::vector<Change>* _changes;
    Symbol* _replacement = nullptr;
    bool _replacementBitmap = false;
    bool _replacementSound = false;
};

/// Calls \a visit for every keyframe of the document, with where it is.
void forEachFrame(Document& document, const std::function<void(Match&, Frame*)>& visit)
{
    const auto timeline = [&](Timeline* timeline, Symbol* symbol, int scene, const std::string& prefix) {
        if (!timeline)
            return;
        for (size_t l = 0; l < timeline->layers.size(); ++l)
        {
            Layer* layer = timeline->layers[l];
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                Match match;
                match.where = prefix + " > " + layer->name + " > Frame " + std::to_string(frame->index + 1);
                match.symbol = symbol;
                match.scene = scene;
                match.layer = static_cast<int>(l);
                match.frame = frame->index;
                visit(match, frame);
            }
        }
    };
    for (size_t i = 0; i < document.timelines.size(); ++i)
        timeline(document.timelines[i], nullptr, static_cast<int>(i),
            document.timelines[i] ? document.timelines[i]->name : std::string());
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                for (Timeline* t : symbol->timelines)
                    timeline(t, symbol, -1, symbol->name);
}

/// Calls \a visit for every object on every keyframe of the document,
/// with where it is.
void forEachObject(Document& document, const std::function<void(Match&, Element*)>& visit)
{
    const auto timeline = [&](Timeline* timeline, Symbol* symbol, int scene, const std::string& prefix) {
        if (!timeline)
            return;
        for (size_t l = 0; l < timeline->layers.size(); ++l)
        {
            Layer* layer = timeline->layers[l];
            if (!layer)
                continue;
            for (Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                for (Element* element : frame->elements)
                {
                    Match match;
                    match.where = prefix + " > " + layer->name + " > Frame " + std::to_string(frame->index + 1);
                    match.symbol = symbol;
                    match.scene = scene;
                    match.layer = static_cast<int>(l);
                    match.frame = frame->index;
                    match.topElement = element;
                    match.element = element;
                    visit(match, element);
                }
            }
        }
    };
    for (size_t i = 0; i < document.timelines.size(); ++i)
        timeline(document.timelines[i], nullptr, static_cast<int>(i),
            document.timelines[i] ? document.timelines[i]->name : std::string());
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                for (Timeline* t : symbol->timelines)
                    timeline(t, symbol, -1, symbol->name);
}

bool searchable(const Query& query)
{
    switch (query.kind)
    {
    case Kind::Text:
    case Kind::Font:
    case Kind::Symbol:
    case Kind::Sound:
    case Kind::Bitmap:
        return !query.find.empty();
    case Kind::Color:
        return true;
    }
    return false;
}

} // namespace

std::string replaced(const std::string& text, const Query& query, int& count)
{
    count = 0;
    const std::string& find = query.find;
    if (find.empty() || text.size() < find.size())
        return text;
    std::string out;
    size_t i = 0;
    while (i < text.size())
    {
        bool hit = i + find.size() <= text.size();
        for (size_t k = 0; hit && k < find.size(); ++k)
            hit = folded(text[i + k], query.matchCase) == folded(find[k], query.matchCase);
        if (hit && query.wholeWord)
        {
            const bool before = i > 0 && wordCharacter(text[i - 1]);
            const bool after = i + find.size() < text.size() && wordCharacter(text[i + find.size()]);
            hit = !before && !after;
        }
        if (hit)
        {
            out += query.replace;
            i += find.size();
            ++count;
        }
        else
        {
            out += text[i++];
        }
    }
    return out;
}

std::vector<Match> findAll(Document& document, const Query& query)
{
    std::vector<Match> matches;
    if (!searchable(query))
        return matches;
    Searcher searcher(document, query, nullptr);
    if (query.kind == Kind::Sound)
    {
        forEachFrame(document, [&](Match& match, Frame* frame) {
            match.count = searcher.visitFrame(frame);
            if (match.count > 0)
                matches.push_back(match);
        });
        return matches;
    }
    forEachObject(document, [&](Match& match, Element* element) {
        // An object inside a group is listed on its own, at the group.
        std::function<void(Element*)> look = [&](Element* object) {
            if (object && object->elementType() == Element::Type::Group)
            {
                for (Element* member : static_cast<Group*>(object)->members)
                    look(member);
                return;
            }
            const int count = searcher.visit(object);
            if (count > 0)
            {
                Match found = match;
                found.element = object;
                found.count = count;
                matches.push_back(found);
            }
        };
        look(element);
    });
    return matches;
}

CommandPtr replaceAll(Document& document, const Query& query, int* replaced)
{
    if (replaced)
        *replaced = 0;
    if (!searchable(query))
        return nullptr;
    std::vector<Change> changes;
    Searcher searcher(document, query, &changes);
    if (!searcher.canReplace())
        return nullptr;
    int count = 0;
    if (query.kind == Kind::Sound)
        forEachFrame(document, [&](Match&, Frame* frame) { count += searcher.visitFrame(frame); });
    else
        forEachObject(document, [&](Match&, Element* element) { count += searcher.visit(element); });
    if (changes.empty())
        return nullptr;
    if (replaced)
        *replaced = count;
    static const char* const names[] = {"Replace Text", "Replace Font", "Replace Color", "Replace Symbol",
        "Replace Sound", "Replace Bitmap"};
    return CommandPtr(new ReplaceCommand(std::move(changes), names[static_cast<int>(query.kind)]));
}

std::vector<std::string> fontsUsed(Document& document)
{
    std::set<std::string> faces;
    std::function<void(Element*)> look = [&](Element* element) {
        if (!element)
            return;
        if (element->elementType() == Element::Type::StaticText)
            for (const TextRun& run : static_cast<StaticText*>(element)->runs)
                if (!run.face.empty())
                    faces.insert(run.face);
        if (element->elementType() == Element::Type::Group)
            for (Element* member : static_cast<Group*>(element)->members)
                look(member);
    };
    forEachObject(document, [&](Match&, Element* element) { look(element); });
    return std::vector<std::string>(faces.begin(), faces.end());
}

} // namespace find

} // namespace fla
