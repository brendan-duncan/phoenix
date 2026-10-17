#include "html5_actionscript2.h"

#include "script_edit.h"
#include "swf_movie.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../play/script/class_path.h"

#include "js/as2.h"
#include "tinyxml2.h"

#include <cstring>

namespace fla {

namespace html5as2 {

namespace {

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const char* name)
{
    for (const auto& [key, value] : attributes)
        if (key == name)
            return value;
    return std::string();
}

/// Whether the scripts have \a marks, frame scripts and (with \a instances)
/// the handlers on instances.
bool scriptsHave(const Document& document, const std::vector<const char*>& marks, bool instances)
{
    std::vector<const Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    for (const Timeline* timeline : timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                if (instances)
                    for (const Element* element : frame->elements)
                        if (element && element->elementType() == Element::Type::SymbolInstance &&
                            !scripts::instanceScript(*element).empty())
                            return true;
                if (frame->actionScript)
                    for (const char* mark : marks)
                        if (frame->actionScript->code.find(mark) != std::string::npos)
                            return true;
            }
    return false;
}

} // namespace

bool usesActionScript2(const Document& document)
{
    const int version = play::actionScriptVersion(document);
    if (version == 1 || version == 2)
        return true;
    // The marks Test Movie goes by (MovieRuntime's scriptsLookLike...).
    static const std::vector<const char*> older = {"setProperty(", "getProperty(", "tellTarget", "ifFrameLoaded",
        "onClipEvent", "_root.", "_parent.", "._visible", "._x", "attachMovie("};
    static const std::vector<const char*> newer = {"import flash.", "import fl.", "addEventListener", ":void", "package ",
        "MouseEvent.", "Event.ENTER_FRAME", "new Sprite(", "new MovieClip("};
    if (version == 3 || version == 0)
        return scriptsHave(document, older, true) && !scriptsHave(document, newer, false) &&
            play::documentClassOf(document).empty();
    return false;
}

int swfVersion(const Document& document)
{
    for (const auto& [path, bytes] : document.extraFiles)
    {
        if (path != "PublishSettings.xml")
            continue;
        const std::vector<uint8_t>& data = bytes.get();
        tinyxml2::XMLDocument xml;
        if (xml.Parse(reinterpret_cast<const char*>(data.data()), data.size()) != tinyxml2::XML_SUCCESS)
            break;
        // The Flash format's own Version (its player), wherever the
        // profile keeps it.
        std::vector<const tinyxml2::XMLElement*> pending{xml.RootElement()};
        while (!pending.empty())
        {
            const tinyxml2::XMLElement* at = pending.back();
            pending.pop_back();
            if (!at)
                continue;
            if (std::strcmp(at->Name(), "PublishFlashProperties") == 0)
                if (const tinyxml2::XMLElement* version = at->FirstChildElement("Version"))
                    if (version->GetText())
                    {
                        const int number = std::atoi(version->GetText());
                        return number > 0 ? std::max(number, 4) : 8;
                    }
            for (const tinyxml2::XMLElement* child = at->FirstChildElement(); child; child = child->NextSiblingElement())
                pending.push_back(child);
        }
    }
    return 8;
}

Result translate(const Document& document, const html5::ActionScript2& scripts,
    const std::function<std::optional<std::string>(const std::string& qualifiedName)>& findClass,
    std::vector<std::string>* notes)
{
    as3::js::as2::Movie movie;
    for (const auto& [name, source] : swfmovie::actionScript2Classes(document, findClass))
    {
        const size_t dot = name.find_last_of('.');
        movie.classes.push_back({source, (dot == std::string::npos ? name : name.substr(dot + 1)) + ".as"});
    }
    for (const html5::ActionScript2::FrameScript& frame : scripts.frames)
        movie.frames.push_back({frame.timeline, frame.frame, frame.code, frame.scene, frame.where});
    for (const html5::ActionScript2::InstanceScript& instance : scripts.instances)
        movie.instances.push_back({instance.id, instance.code, instance.where});
    // Symbols linked to classes: Object.registerClass(identifier, class).
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
        {
            if (!symbol)
                continue;
            const std::string identifier = attributeOf(symbol->extraAttributes, "linkageIdentifier");
            const std::string className = attributeOf(symbol->extraAttributes, "linkageClassName");
            if (!identifier.empty() && !className.empty())
                movie.registrations.emplace_back(identifier, className);
        }

    as3::js::as2::Options options;
    options.swfVersion = swfVersion(document);
    const as3::js::as2::Output out = as3::js::as2::translate(movie, options);
    if (notes)
        for (const std::string& error : out.errors)
            notes->push_back("ActionScript left out: " + error);
    Result result;
    result.code = out.code;
    result.classes = out.classes;
    return result;
}

const std::string& avm1Runtime()
{
    return as3::js::as2::runtime();
}

} // namespace html5as2

} // namespace fla
