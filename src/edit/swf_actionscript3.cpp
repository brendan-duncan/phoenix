#include "swf_actionscript3.h"

#include "../data/action_script.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/raw_element.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "swf_movie.h"
#include "timeline_edit.h"

#include "abc/abc.h"
#include "abc/abc_player.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

namespace fla {
namespace swfmovie {

namespace {

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& name)
{
    for (const auto& attribute : attributes)
        if (attribute.first == name)
            return attribute.second;
    return std::string();
}

/// The first setting named \a name in the document's publish settings, or
/// empty; \a found says whether there was one.
std::string publishSetting(const Document& document, const char* name, bool* found = nullptr)
{
    if (found)
        *found = false;
    for (const auto& [path, bytes] : document.extraFiles)
    {
        if (path != "PublishSettings.xml")
            continue;
        const std::vector<uint8_t>& data = bytes.get();
        tinyxml2::XMLDocument xml;
        if (xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) != tinyxml2::XML_SUCCESS)
            return std::string();
        std::function<const tinyxml2::XMLElement*(const tinyxml2::XMLElement*)> find =
            [&](const tinyxml2::XMLElement* at) -> const tinyxml2::XMLElement* {
            for (const tinyxml2::XMLElement* child = at ? at->FirstChildElement() : nullptr; child;
                 child = child->NextSiblingElement())
            {
                if (std::strcmp(child->Name(), name) == 0)
                    return child;
                if (const tinyxml2::XMLElement* deeper = find(child))
                    return deeper;
            }
            return nullptr;
        };
        const tinyxml2::XMLElement* setting = find(xml.RootElement());
        if (found)
            *found = setting != nullptr;
        return setting && setting->GetText() ? setting->GetText() : std::string();
    }
    return std::string();
}

int timelineLength(const Timeline* timeline)
{
    int length = 0;
    for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
        if (layer)
            length = std::max(length, timeline::layerEnd(*layer));
    return std::max(1, length);
}

bool isComponent(const Element* element)
{
    return element && element->elementType() == Element::Type::Raw &&
        static_cast<const RawElement*>(element)->xml.find("<DOMComponentInstance") != std::string::npos;
}

const Symbol* symbolNamed(const Document& document, const std::string& name)
{
    if (!document.symbolList)
        return nullptr;
    const auto found = document.symbolList->symbolMap.find(name);
    return found == document.symbolList->symbolMap.end() ? nullptr : found->second;
}

std::string packageOf(const std::string& qualified)
{
    const size_t dot = qualified.find_last_of('.');
    return dot == std::string::npos ? std::string() : qualified.substr(0, dot);
}

std::string shortOf(const std::string& qualified)
{
    const size_t dot = qualified.find_last_of('.');
    return dot == std::string::npos ? qualified : qualified.substr(dot + 1);
}

/// Whether the player has \a qualified, so it needs no source.
bool playerClass(const std::string& qualified)
{
    const std::string package = packageOf(qualified);
    if (package == "flash" || package.rfind("flash.", 0) == 0)
        return true;
    if (package.empty())
        return as3::abc::isTopLevelName(qualified);
    return as3::abc::findPlayerClass(package, shortOf(qualified)) != nullptr;
}

bool wordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

/// What a source says of the names it uses: its package, its wildcard
/// imports, the words of its code (dotted paths whole) and its strings.
struct Scan
{
    std::string package;
    std::vector<std::string> wildcards;
    std::map<std::string, std::string> imports;
    std::vector<std::string> words;
    std::vector<std::string> strings;
};

Scan scan(const std::string& text)
{
    Scan out;
    std::string previous;
    size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n')
                ++i;
        }
        else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            const size_t end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
        }
        else if (c == '"' || c == '\'')
        {
            std::string literal;
            size_t j = i + 1;
            while (j < text.size() && text[j] != c && text[j] != '\n')
            {
                if (text[j] == '\\' && j + 1 < text.size())
                    ++j;
                literal += text[j++];
            }
            out.strings.push_back(literal);
            i = j + 1;
        }
        else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$')
        {
            size_t j = i;
            while (j < text.size() && (wordChar(text[j]) || (text[j] == '.' && j + 1 < text.size() &&
                                                                (wordChar(text[j + 1]) || text[j + 1] == '*'))))
                ++j;
            std::string word = text.substr(i, j - i);
            const bool wildcard = j < text.size() && text[j] == '*' && !word.empty() && word.back() == '.';
            while (!word.empty() && word.back() == '.')
                word.pop_back();
            if (previous == "package")
                out.package = word;
            else if (previous == "import" && wildcard)
                out.wildcards.push_back(word);
            else if (previous == "import")
                out.imports[shortOf(word)] = word;
            out.words.push_back(word);
            previous = word;
            i = j + (wildcard ? 1 : 0);
        }
        else
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
                previous.clear();
            ++i;
        }
    }
    return out;
}

/// Whether \a source defines \a name: a class, interface, function,
/// variable, constant or namespace of that name.
bool declares(const std::string& source, const std::string& name)
{
    static const char* const kinds[] = {"class", "interface", "function", "var", "const", "namespace"};
    for (size_t at = source.find(name); at != std::string::npos; at = source.find(name, at + 1))
    {
        if ((at > 0 && wordChar(source[at - 1])) || (at + name.size() < source.size() && wordChar(source[at + name.size()])))
            continue;
        size_t end = at;
        while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1])))
            --end;
        size_t start = end;
        while (start > 0 && wordChar(source[start - 1]))
            --start;
        const std::string before = source.substr(start, end - start);
        for (const char* kind : kinds)
            if (before == kind)
                return true;
    }
    return false;
}

/// Whether \a source declares a member \a name: a variable, constant,
/// method or accessor.
bool declaresMember(const std::string& source, const std::string& name)
{
    for (size_t at = source.find(name); at != std::string::npos; at = source.find(name, at + 1))
    {
        if ((at > 0 && wordChar(source[at - 1])) || (at + name.size() < source.size() && wordChar(source[at + name.size()])))
            continue;
        size_t end = at;
        while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1])))
            --end;
        size_t start = end;
        while (start > 0 && wordChar(source[start - 1]))
            --start;
        const std::string before = source.substr(start, end - start);
        if (before == "var" || before == "const" || before == "function" || before == "get" || before == "set")
            return true;
    }
    return false;
}

std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '\\' || c == '"')
            out += '\\';
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out + "\"";
}

/// The ActionScript that sets a component's parameters as the player sets
/// them from the inspector: each in turn, by its type, between
/// componentInspectorSetting true and false, one failing not stopping the
/// rest.
std::string parameterCode(const Component& component)
{
    tinyxml2::XMLDocument xml;
    const std::string wrapped = "<parameters>" + component.parameters + "</parameters>";
    if (component.parameters.empty() || xml.Parse(wrapped.c_str(), wrapped.size()) != tinyxml2::XML_SUCCESS)
        return std::string();
    std::string code;
    const auto set = [&](const std::string& name, const std::string& value) {
        code += "try { " + component.name + "." + name + " = " + value + "; } catch (__error:Error) {}\n";
    };
    for (const tinyxml2::XMLElement* property = xml.RootElement()->FirstChildElement("property"); property;
         property = property->NextSiblingElement("property"))
    {
        const tinyxml2::XMLElement* inspectable = property->FirstChildElement("Inspectable");
        if (!inspectable)
            continue;
        const char* variable = inspectable->Attribute("variable");
        const char* named = inspectable->Attribute("name");
        const std::string name = variable && *variable ? variable : named ? named : "";
        const std::string value = inspectable->Attribute("defaultValue") ? inspectable->Attribute("defaultValue") : "";
        const std::string type = inspectable->Attribute("type") ? inspectable->Attribute("type") : "String";
        if (name.empty() || !std::all_of(name.begin(), name.end(), wordChar))
            continue;
        if (type == "Boolean")
            set(name, value == "true" ? "true" : "false");
        else if (type == "Number")
        {
            char* end = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            std::ostringstream text;
            text.precision(17);
            text << number;
            set(name, !value.empty() && end && *end == '\0' ? text.str() : std::string("NaN"));
        }
        else if (type == "Color")
            set(name, std::to_string(std::strtoul(value.c_str() + (!value.empty() && value[0] == '#' ? 1 : 0), nullptr, 16)));
        else if (type == "Array")
        {
            if (value.empty())
                continue;
            std::string list = "[";
            std::stringstream items(value);
            std::string item;
            while (std::getline(items, item, ','))
                list += (list.size() > 1 ? ", " : "") + quoted(item);
            set(name, list + "]");
        }
        else if (type == "Collection")
            continue;
        else
            set(name, quoted(value));
    }
    if (code.empty())
        return std::string();
    return "try { " + component.name + ".componentInspectorSetting = true; } catch (__error:Error) {}\n" + code +
        "try { " + component.name + ".componentInspectorSetting = false; } catch (__error:Error) {}";
}

/// A frame's code from one place: a layer's script, or a component's
/// parameters.
struct Part
{
    std::string where;
    std::string code;
};

/// A timeline compiled as a class: what its frames hold, by frame (from
/// 0, the main timeline's counted across its scenes).
struct TimelineClass
{
    const Symbol* symbol = nullptr;
    as3::abc::Timeline timeline;
    std::map<int, std::vector<Part>> frames;
    /// For each frame, where each part's lines start (from 1).
    std::map<int, std::vector<int>> starts;
    /// The timelines its frames come from: a symbol's, or the scenes.
    std::vector<const Timeline*> timelineSources;
};

/// A named thing on a timeline's stage: of a symbol, or of a class.
struct Named
{
    std::string name;
    const Symbol* symbol = nullptr;
    std::string type;
};

class Compiler
{
public:
    Compiler(const Document& document, const ActionScript3Options& options)
        : _document(document)
        , _options(options)
        , _components(components(document))
    {}

    ActionScript3 run()
    {
        ActionScript3 out;
        bool known = false;
        const std::string declare = publishSetting(_document, "AS3AutoDeclare", &known);
        _autoDeclare = !known || std::atoi(declare.c_str()) != 0;
        const std::string documentName = _options.documentName.empty() ? std::string("Untitled") : _options.documentName;

        // The symbols' classes: the class each is linked to, or one for its
        // frame scripts; a linked class with a file of its own, extended
        // by one for the frame scripts when it has them.
        int index = 0;
        if (_document.symbolList)
            for (const auto& [name, symbol] : _document.symbolList->symbolMap)
            {
                if (!symbol || symbol->timelines.empty() || symbol->symbolType == SymbolType::Graphic)
                    continue;
                const bool button = symbol->symbolType == SymbolType::Button;
                const std::string linked = attributeOf(symbol->extraAttributes, "linkageClassName");
                std::string base = attributeOf(symbol->extraAttributes, "linkageBaseClass");
                if (base.empty())
                    base = button ? "flash.display.SimpleButton" : "flash.display.MovieClip";
                TimelineClass made;
                made.symbol = symbol;
                if (!button)
                    gather(made, symbol->timelines.front(), "symbol " + symbol->name, 0);
                const bool scripted = !made.frames.empty();
                if (linked.empty() && !scripted)
                    continue;
                ++index;
                if (!linked.empty() && (playerClass(linked) || find(linked)))
                {
                    if (!scripted)
                    {
                        _classes[symbol] = linked;
                        continue;
                    }
                    // Its frame scripts in a class of their own, under it.
                    made.timeline.className = as3::abc::timelineClassName(documentName, symbol->name, index);
                    made.timeline.baseClass = linked;
                    _notes.push_back("symbol " + symbol->name + ": its frame scripts are in " +
                        made.timeline.className + ", under its class " + linked + ", so a " + shortOf(linked) +
                        " made in code has none");
                }
                else
                {
                    made.timeline.className =
                        linked.empty() ? as3::abc::timelineClassName(documentName, symbol->name, index) : linked;
                    made.timeline.baseClass = base;
                }
                _classes[symbol] = made.timeline.className;
                _timelines.push_back(std::move(made));
            }

        // The main timeline: its scenes one after another, as the document
        // class or a class under it.
        TimelineClass main;
        int start = 0;
        for (const Timeline* scene : _document.timelines)
        {
            if (!scene)
                continue;
            gather(main, scene, scene->name, start);
            start += timelineLength(scene);
        }
        std::string documentClass = publishSetting(_document, "DocumentClass");
        documentClass.erase(0, documentClass.find_first_not_of(" \t\r\n"));
        documentClass.erase(documentClass.find_last_not_of(" \t\r\n") + 1);
        const bool staged = !named(main).empty() && _autoDeclare;
        if (!documentClass.empty() && (playerClass(documentClass) || find(documentClass)))
        {
            if (!main.frames.empty() || staged)
            {
                main.timeline.className = as3::abc::timelineClassName(documentName);
                main.timeline.baseClass = documentClass;
            }
            else
                out.mainClass = documentClass;
        }
        else
        {
            if (!documentClass.empty())
                _notes.push_back("document class " + documentClass +
                    ": no class file of that name on the class path, so one is made for the main timeline");
            if (!documentClass.empty() || !main.frames.empty() || staged)
                main.timeline.className =
                    documentClass.empty() ? as3::abc::timelineClassName(documentName) : documentClass;
        }
        if (!main.timeline.className.empty())
        {
            out.mainClass = main.timeline.className;
            _timelines.push_back(std::move(main));
        }

        // The classes the scripts reach, and what those reach in turn.
        for (const TimelineClass& made : _timelines)
            for (const auto& [frame, parts] : made.frames)
                for (const Part& part : parts)
                    reach(part.code);
        while (!_pending.empty())
        {
            const std::string source = std::move(_pending.back());
            _pending.pop_back();
            reach(source);
        }

        // Each timeline's text, its stage instances, typed.
        for (TimelineClass& made : _timelines)
        {
            for (const auto& [frame, parts] : made.frames)
            {
                std::string code;
                std::vector<int>& starts = made.starts[frame];
                int line = 1;
                for (const Part& part : parts)
                {
                    starts.push_back(line);
                    code += part.code + "\n";
                    line += static_cast<int>(std::count(part.code.begin(), part.code.end(), '\n')) + 1;
                }
                made.timeline.frames.push_back({frame, code});
            }
            const std::string baseSource = sourceOf(made.timeline.baseClass);
            for (const Named& one : named(made))
            {
                const bool component = one.name.rfind("__id", 0) == 0;
                if (!_autoDeclare && !component)
                    continue;
                // What the class it is under declares is the player's to set.
                if (!baseSource.empty() && declaresMember(baseSource, one.name))
                    continue;
                made.timeline.instances.push_back({one.name, one.type});
            }
        }

        std::vector<as3::abc::Source> sources;
        for (const auto& [name, found] : _found)
            if (found.has_value())
                sources.push_back({found->first, found->second});
        std::vector<as3::abc::Timeline> timelines;
        for (const TimelineClass& made : _timelines)
            timelines.push_back(made.timeline);
        missingSkins();
        if (sources.empty() && timelines.empty())
        {
            out.notes = _notes;
            return out;
        }

        as3::abc::Options compile;
        compile.includeHandler = _options.readFile;
        // Conditional compilation constants: CONFIG::debug="true";...
        std::stringstream constants(publishSetting(_document, "AS3ConfigConst"));
        std::string constant;
        while (std::getline(constants, constant, ';'))
        {
            const size_t equals = constant.find('=');
            if (equals == std::string::npos)
                continue;
            std::string name = constant.substr(0, equals);
            std::string value = constant.substr(equals + 1);
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (!name.empty())
                compile.constants[name] = value;
        }
        const as3::abc::Output compiled = as3::abc::compile(sources, timelines, compile);
        for (const as3::abc::Diagnostic& diagnostic : compiled.diagnostics)
            _notes.push_back(describe(diagnostic));
        if (!compiled.ok)
            _notes.push_back("The ActionScript 3 did not compile: the movie goes out without its scripts");
        else
        {
            out.abc = compiled.abc;
            out.defined = compiled.classes;
            out.classes = _classes;
        }
        if (!compiled.ok)
            out.mainClass.clear();
        out.notes = _notes;
        return out;
    }

private:
    const Document& _document;
    const ActionScript3Options& _options;
    std::map<const RawElement*, Component> _components;
    bool _autoDeclare = true;
    std::vector<TimelineClass> _timelines;
    std::map<const Symbol*, std::string> _classes;
    std::vector<std::string> _notes;
    /// Each class looked for: its source and file, or none.
    std::map<std::string, std::optional<std::pair<std::string, std::string>>> _found;
    std::vector<std::string> _pending;

    /// The source of \a qualified, looked for once; null for none.
    const std::pair<std::string, std::string>* find(const std::string& qualified)
    {
        if (qualified.empty() || playerClass(qualified))
            return nullptr;
        const auto known = _found.find(qualified);
        if (known != _found.end())
            return known->second ? &*known->second : nullptr;
        std::optional<std::pair<std::string, std::string>>& entry = _found[qualified];
        if (!_options.findClass)
            return nullptr;
        std::optional<std::string> source = _options.findClass(qualified);
        // A file found by another name (as a file system that ignores case
        // finds one) is not this class.
        if (!source || !declares(*source, shortOf(qualified)))
            return nullptr;
        std::string path = qualified;
        std::replace(path.begin(), path.end(), '.', '/');
        entry = std::make_pair(*source, path + ".as");
        _pending.push_back(*source);
        return &*entry;
    }

    std::string sourceOf(const std::string& qualified)
    {
        const auto known = _found.find(qualified);
        return known != _found.end() && known->second ? known->second->first : std::string();
    }

    /// Looks for the classes \a text names: dotted paths, and short names
    /// in its own package, the top level or its wildcard imports.
    void reach(const std::string& text)
    {
        const Scan seen = scan(text);
        std::set<std::string> asked;
        const auto ask = [&](const std::string& name) {
            const std::string last = shortOf(name);
            if (!last.empty() && std::isupper(static_cast<unsigned char>(last[0])) && asked.insert(name).second)
                find(name);
        };
        for (const std::string& word : seen.words)
        {
            // Each prefix of a dotted path: com.studio.Hat.make names
            // com.studio.Hat.
            for (size_t end = word.find('.'); end != std::string::npos; end = word.find('.', end + 1))
                ask(word.substr(0, end));
            if (word.find('.') != std::string::npos)
                ask(word);
            const std::string first = word.substr(0, word.find('.'));
            if (first.empty() || !std::isupper(static_cast<unsigned char>(first[0])))
                continue;
            const auto imported = seen.imports.find(first);
            if (imported != seen.imports.end())
                ask(imported->second);
            ask(first);
            if (!seen.package.empty())
                ask(seen.package + "." + first);
            for (const std::string& wildcard : seen.wildcards)
                ask(wildcard + "." + first);
        }
    }

    /// \a timeline's frame scripts and components' parameters into
    /// \a made, its frames counted from \a start; \a where names it.
    void gather(TimelineClass& made, const Timeline* timeline, const std::string& where, int start)
    {
        if (!timeline)
            return;
        // Components' parameters first, set before the frame's scripts run.
        for (const Layer* layer : timeline->layers)
        {
            if (!layer || layer->layerType == Layer::Type::Folder)
                continue;
            std::set<std::string> before;
            for (const Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                std::set<std::string> now;
                for (const Element* element : frame->elements)
                {
                    if (!isComponent(element))
                        continue;
                    const auto found = _components.find(static_cast<const RawElement*>(element));
                    if (found == _components.end())
                        continue;
                    const Component& component = found->second;
                    const std::string key = component.name + "\n" + component.symbol + "\n" + component.parameters;
                    now.insert(key);
                    // Placed on an earlier keyframe and kept: set then.
                    if (before.count(key))
                        continue;
                    const std::string code = parameterCode(component);
                    if (!code.empty())
                        made.frames[start + frame->index].push_back(
                            {where + ", component " + component.name + ", frame " + std::to_string(frame->index + 1), code});
                }
                before = now;
            }
        }
        // Then the scripts, the top layer's first.
        for (const Layer* layer : timeline->layers)
        {
            if (!layer || layer->layerType == Layer::Type::Folder)
                continue;
            for (const Frame* frame : layer->frames)
                if (frame && frame->actionScript && !frame->actionScript->code.empty())
                    made.frames[start + frame->index].push_back({where + ", layer " + layer->name + ", frame " +
                        std::to_string(frame->index + 1), frame->actionScript->code});
        }
        made.timelineSources.push_back(timeline);
    }

    /// The named things on \a made's stage, each once, typed as the
    /// player makes them.
    std::vector<Named> named(const TimelineClass& made)
    {
        std::vector<Named> out;
        const auto add = [&](const std::string& name, const std::string& type) {
            if (name.empty() || !std::all_of(name.begin(), name.end(), wordChar) ||
                std::isdigit(static_cast<unsigned char>(name[0])))
                return;
            for (Named& one : out)
                if (one.name == name)
                {
                    // Of different kinds on different frames: untyped.
                    if (one.type != type)
                        one.type.clear();
                    return;
                }
            out.push_back({name, nullptr, type});
        };
        std::function<void(const Element*)> visit = [&](const Element* element) {
            if (!element)
                return;
            if (element->elementType() == Element::Type::SymbolInstance)
            {
                const auto* instance = static_cast<const SymbolInstance*>(element);
                if (instance->symbolType != SymbolType::Graphic && instance->symbol)
                    add(instance->name, typeOf(instance->symbol));
            }
            else if (element->elementType() == Element::Type::StaticText)
            {
                const auto* text = static_cast<const StaticText*>(element);
                if (text->textType != StaticText::TextType::Static)
                    add(fieldName(*text), "flash.text.TextField");
            }
            else if (element->elementType() == Element::Type::Group)
                for (const Element* member : static_cast<const Group*>(element)->members)
                    visit(member);
            else if (isComponent(element))
            {
                const auto found = _components.find(static_cast<const RawElement*>(element));
                if (found != _components.end())
                    add(found->second.name, typeOf(symbolNamed(_document, found->second.symbol)));
            }
        };
        for (const Timeline* timeline : made.timelineSources)
            for (const Layer* layer : timeline->layers)
                for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                    for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                        visit(element);
        return out;
    }

    /// The class an instance of \a symbol is: its own, when it is one the
    /// movie has, else the player's for its kind.
    std::string typeOf(const Symbol* symbol)
    {
        if (!symbol)
            return std::string();
        const auto own = _classes.find(symbol);
        if (own != _classes.end())
        {
            bool made = false;
            for (const TimelineClass& timeline : _timelines)
                made = made || timeline.timeline.className == own->second;
            if (made || playerClass(own->second) || find(own->second))
                return own->second;
        }
        return symbol->symbolType == SymbolType::Button ? "flash.display.SimpleButton" : "flash.display.MovieClip";
    }

    /// The skins the components the movie has name that its library has
    /// no symbol for: they are drawn in a plain look of Phoenix's own.
    void missingSkins()
    {
        std::set<std::string> linked;
        if (_document.symbolList)
            for (const auto& [name, symbol] : _document.symbolList->symbolMap)
                if (symbol)
                    linked.insert(attributeOf(symbol->extraAttributes, "linkageClassName"));
        std::set<std::string> missing;
        for (const auto& [name, found] : _found)
        {
            if (!found || name.rfind("fl.controls.", 0) != 0 || name.rfind("fl.controls.listClasses.", 0) == 0)
                continue;
            for (const std::string& literal : scan(found->first).strings)
            {
                const size_t bar = literal.find('_');
                const bool skin = bar != std::string::npos && bar > 0 && bar + 1 < literal.size() &&
                    std::isupper(static_cast<unsigned char>(literal[0])) &&
                    std::all_of(literal.begin(), literal.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; });
                if (skin && !linked.count(literal))
                    missing.insert(literal);
            }
        }
        if (missing.empty())
            return;
        std::string list;
        for (const std::string& name : missing)
            list += (list.empty() ? "" : ", ") + name;
        _notes.push_back("The components' skins " + list +
            " are not in the library (its Component Assets): Phoenix's plain look (fl.skins.DefaultSkin) draws them");
    }

    /// A diagnostic with where it is: a class file and line, or a
    /// timeline's scene or symbol, layer, frame and line.
    std::string describe(const as3::abc::Diagnostic& diagnostic) const
    {
        const std::string kind = diagnostic.isError ? std::string() : std::string("warning: ");
        for (const TimelineClass& made : _timelines)
        {
            const std::string prefix = made.timeline.className + ", frame ";
            if (diagnostic.fileName.rfind(prefix, 0) != 0)
                continue;
            const int frame = std::atoi(diagnostic.fileName.c_str() + prefix.size()) - 1;
            const auto parts = made.frames.find(frame);
            const auto starts = made.starts.find(frame);
            if (parts == made.frames.end() || starts == made.starts.end() || parts->second.empty())
                break;
            size_t which = 0;
            for (size_t i = 0; i < starts->second.size(); ++i)
                if (diagnostic.line >= starts->second[i])
                    which = i;
            const int line = diagnostic.line > 0 ? diagnostic.line - starts->second[which] + 1 : 0;
            return parts->second[which].where + (line > 0 ? ", line " + std::to_string(line) : std::string()) + ": " +
                kind + diagnostic.message;
        }
        return (diagnostic.fileName.empty() ? std::string("ActionScript") : diagnostic.fileName) +
            (diagnostic.line > 0 ? ", line " + std::to_string(diagnostic.line) : std::string()) + ": " + kind +
            diagnostic.message;
    }
};

} // namespace

std::map<const RawElement*, Component> components(const Document& document)
{
    std::map<const RawElement*, Component> out;
    int unnamed = 0;
    std::vector<const Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (const auto& [name, symbol] : document.symbolList->symbolMap)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (const Element* element : frame ? frame->elements : std::vector<Element*>())
                {
                    if (!isComponent(element))
                        continue;
                    const auto* raw = static_cast<const RawElement*>(element);
                    tinyxml2::XMLDocument xml;
                    if (xml.Parse(raw->xml.c_str(), raw->xml.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement())
                        continue;
                    const tinyxml2::XMLElement* root = xml.RootElement();
                    Component component;
                    component.symbol = root->Attribute("libraryItemName") ? root->Attribute("libraryItemName") : "";
                    component.name = root->Attribute("name") ? root->Attribute("name") : "";
                    if (component.name.empty())
                        component.name = "__id" + std::to_string(unnamed++) + "_";
                    if (const tinyxml2::XMLElement* matrix = root->FirstChildElement("matrix"))
                        if (const tinyxml2::XMLElement* m = matrix->FirstChildElement("Matrix"))
                            component.matrix = swf::Matrix{m->DoubleAttribute("a", 1.0), m->DoubleAttribute("b", 0.0),
                                m->DoubleAttribute("c", 0.0), m->DoubleAttribute("d", 1.0), m->DoubleAttribute("tx", 0.0),
                                m->DoubleAttribute("ty", 0.0)};
                    if (const tinyxml2::XMLElement* parameters = root->FirstChildElement("parametersAsXML"))
                        if (parameters->GetText())
                            component.parameters = parameters->GetText();
                    out[raw] = component;
                }
    return out;
}

ActionScript3 compileActionScript3(const Document& document, const ActionScript3Options& options)
{
    return Compiler(document, options).run();
}

std::vector<std::string> writeActionScript3(const Document& document, swf::Writer& writer,
    const std::map<const Symbol*, int>& sprites, const ActionScript3Options& options)
{
    const ActionScript3 compiled = compileActionScript3(document, options);
    std::vector<std::string> notes = compiled.notes;
    if (compiled.abc.empty())
        return notes;
    writer.doABC(options.documentName.empty() ? std::string("frame1") : options.documentName, compiled.abc);
    const std::set<std::string> defined(compiled.defined.begin(), compiled.defined.end());
    const auto has = [&](const std::string& name) { return defined.count(name) || playerClass(name); };
    std::vector<std::pair<int, std::string>> classes;
    if (!compiled.mainClass.empty() && has(compiled.mainClass))
        classes.emplace_back(0, compiled.mainClass);
    for (const auto& [symbol, name] : compiled.classes)
    {
        const auto sprite = sprites.find(symbol);
        if (sprite == sprites.end() || !sprite->second)
            continue;
        if (has(name))
            classes.emplace_back(sprite->second, name);
        else
            notes.push_back("symbol " + symbol->name + ": its class " + name + " is not in the movie");
    }
    writer.symbolClass(classes);
    return notes;
}

} // namespace swfmovie
} // namespace fla
