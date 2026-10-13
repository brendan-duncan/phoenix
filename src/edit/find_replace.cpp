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
#include <map>
#include <set>

namespace fla {

namespace find {

namespace {

/// One field changed, both ways. The field is found again each time by its
/// place among every field of its kind a search walks over (its ordinal),
/// never by a pointer: merges, frame edits and undo rebuild fills, text runs
/// and frames, while the history always brings the document back to the
/// state the change was recorded in, where the same walk finds the same
/// field.
struct Change
{
    size_t ordinal = 0;
    std::string textBefore;
    std::string textAfter;
    uint8_t colorBefore[3] = {};
    uint8_t colorAfter[3] = {};
    Symbol* symbolBefore = nullptr;
    Symbol* symbolAfter = nullptr;
};

void apply(Document& document, const Query& query, const std::vector<Change>& changes, bool forward);

class ReplaceCommand : public Command
{
public:
    ReplaceCommand(Document& document, Query query, std::vector<Change> changes, std::string name)
        : _document(document)
        , _query(std::move(query))
        , _changes(std::move(changes))
        , _name(std::move(name))
    {
    }

    void redo() override { apply(_document, _query, _changes, true); }

    void undo() override { apply(_document, _query, _changes, false); }

    std::string name() const override { return _name; }

private:
    Document& _document;
    Query _query;
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

    /// The symbol replacing the one looked for, if any.
    const Symbol* replacement() const { return _replacement; }

    /// Whether what is visited next may be replaced; its fields are counted
    /// either way, so the ordinals stay the same.
    void setRecording(bool recording) { _recording = recording; }

    /// Puts \a changes, by ordinal, into the fields instead of searching:
    /// their after values when \a forward, else their before values.
    void setApplying(const std::map<size_t, const Change*>* changes, bool forward)
    {
        _apply = changes;
        _forward = forward;
    }

    /// How many times \a frame plays the sound looked for.
    int visitFrame(Frame* frame)
    {
        if (_query.kind != Kind::Sound || !frame)
            return 0;
        int count = 0;
        for (auto& attribute : frame->extraAttributes)
        {
            if (attribute.first != "soundName")
                continue;
            const bool hit = attribute.second == _query.find;
            text(attribute.second, hit, _query.replace);
            count += hit ? 1 : 0;
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
                const bool hit = instance->libraryItemName == _query.find;
                text(instance->libraryItemName, hit, _query.replace);
                count += hit ? 1 : 0;
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
                const size_t ordinal = _ordinal++;
                if (const Change* change = applying(ordinal))
                {
                    instance->libraryItemName = _forward ? change->textAfter : change->textBefore;
                    instance->symbol = _forward ? change->symbolAfter : change->symbolBefore;
                    break;
                }
                if (instance->libraryItemName == _query.find)
                {
                    ++count;
                    if (_changes && _recording && _replacement)
                    {
                        Change change;
                        change.ordinal = ordinal;
                        change.textBefore = instance->libraryItemName;
                        change.textAfter = _query.replace;
                        change.symbolBefore = instance->symbol;
                        change.symbolAfter = _replacement;
                        _changes->push_back(change);
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
            const std::string after = _apply ? std::string() : replaced(run.text, _query, count);
            text(run.text, count > 0, after);
            return count;
        }
        case Kind::Font:
        {
            const bool hit = !_query.find.empty() && sameFace(run.face, _query.find);
            text(run.face, hit && !_query.replace.empty(), _query.replace);
            return hit ? 1 : 0;
        }
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
            if (!bitmap)
                return 0;
            const bool hit = bitmap->bitmapPath == _query.find;
            text(bitmap->bitmapPath, hit, _query.replace);
            return hit ? 1 : 0;
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
        const size_t ordinal = _ordinal++;
        if (const Change* change = applying(ordinal))
        {
            const uint8_t* value = _forward ? change->colorAfter : change->colorBefore;
            std::copy(value, value + 3, color);
            return 0;
        }
        if (color[0] != _query.color[0] || color[1] != _query.color[1] || color[2] != _query.color[2])
            return 0;
        if (_changes && _recording)
        {
            Change change;
            change.ordinal = ordinal;
            std::copy(color, color + 3, change.colorBefore);
            std::copy(_query.replaceColor, _query.replaceColor + 3, change.colorAfter);
            _changes->push_back(change);
        }
        return 1;
    }

    /// A text field of the kind looked for: counted, and replaced with
    /// \a after when it \a matches (or set from its change, when applying).
    void text(std::string& field, bool matches, const std::string& after)
    {
        const size_t ordinal = _ordinal++;
        if (const Change* change = applying(ordinal))
        {
            field = _forward ? change->textAfter : change->textBefore;
            return;
        }
        if (matches && _changes && _recording)
        {
            Change change;
            change.ordinal = ordinal;
            change.textBefore = field;
            change.textAfter = after;
            _changes->push_back(change);
        }
    }

    const Change* applying(size_t ordinal) const
    {
        if (!_apply)
            return nullptr;
        const auto found = _apply->find(ordinal);
        return found == _apply->end() ? nullptr : found->second;
    }

    const Query& _query;
    std::vector<Change>* _changes;
    const std::map<size_t, const Change*>* _apply = nullptr;
    bool _forward = true;
    bool _recording = true;
    size_t _ordinal = 0;
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
        forEachObject(document, [&](Match& match, Element* element) {
            // An instance inside a symbol the replacement contains would put
            // that symbol inside itself: it is left as it is (but still
            // walked, so the fields keep their ordinals).
            const bool recording =
                !(query.kind == Kind::Symbol && symbolContains(searcher.replacement(), match.symbol));
            searcher.setRecording(recording);
            const int found = searcher.visit(element);
            count += recording ? found : 0;
        });
    if (changes.empty())
        return nullptr;
    if (replaced)
        *replaced = count;
    static const char* const names[] = {"Replace Text", "Replace Font", "Replace Color", "Replace Symbol",
        "Replace Sound", "Replace Bitmap"};
    return CommandPtr(new ReplaceCommand(document, query, std::move(changes), names[static_cast<int>(query.kind)]));
}

namespace {

void apply(Document& document, const Query& query, const std::vector<Change>& changes, bool forward)
{
    std::map<size_t, const Change*> byOrdinal;
    for (const Change& change : changes)
        byOrdinal[change.ordinal] = &change;
    Searcher searcher(document, query, nullptr);
    searcher.setApplying(&byOrdinal, forward);
    if (query.kind == Kind::Sound)
        forEachFrame(document, [&](Match&, Frame* frame) { searcher.visitFrame(frame); });
    else
        forEachObject(document, [&](Match&, Element* element) { searcher.visit(element); });
}

} // namespace

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
