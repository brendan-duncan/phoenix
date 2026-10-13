#include "fla_doctor.h"

#include "zip_reader.h"

#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../third_party/tinyxml2/tinyxml2.h"
#include "../third_party/zlib/zlib.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace fla {

namespace doctor {

namespace {

const char* severityName(Issue::Severity severity)
{
    switch (severity)
    {
    case Issue::Severity::Info: return "info";
    case Issue::Severity::Warning: return "warning";
    case Issue::Severity::Error: return "error";
    }
    return "warning";
}

std::string jsonEscaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

void add(Report& report, Issue::Severity severity, const std::string& where, const std::string& what,
    bool repairable = false, bool repaired = false)
{
    Issue issue;
    issue.severity = severity;
    issue.where = where;
    issue.what = what;
    issue.repairable = repairable;
    issue.repaired = repaired;
    report.issues.push_back(issue);
}

/// Every element, inside groups too, with the list holding it.
void forEachElement(std::vector<Element*>& list, const std::function<bool(std::vector<Element*>&, size_t)>& visit)
{
    for (size_t i = 0; i < list.size();)
    {
        // The visitor may take the element out, saying so.
        if (!visit(list, i))
            continue;
        Element* element = list[i];
        if (element && element->elementType() == Element::Type::Group)
            forEachElement(static_cast<Group*>(element)->members, visit);
        ++i;
    }
}

} // namespace

int Report::count(Issue::Severity severity) const
{
    return static_cast<int>(std::count_if(
        issues.begin(), issues.end(), [severity](const Issue& issue) { return issue.severity == severity; }));
}

std::string Report::text() const
{
    std::ostringstream out;
    for (const Issue& issue : issues)
    {
        out << severityName(issue.severity) << ": ";
        if (!issue.where.empty())
            out << issue.where << ": ";
        out << issue.what;
        if (issue.repaired)
            out << " (repaired)";
        else if (issue.repairable)
            out << " (repairable)";
        out << "\n";
    }
    if (salvaged)
        out << "info: the file could not be opened as it is; it was read from what could be salvaged\n";
    out << count(Issue::Severity::Error) << " errors, " << count(Issue::Severity::Warning) << " warnings\n";
    return out.str();
}

std::string Report::json() const
{
    std::ostringstream out;
    out << "{\"readable\":" << (readable ? "true" : "false") << ",\"salvaged\":" << (salvaged ? "true" : "false")
        << ",\"errors\":" << count(Issue::Severity::Error) << ",\"warnings\":" << count(Issue::Severity::Warning)
        << ",\"issues\":[";
    for (size_t i = 0; i < issues.size(); ++i)
    {
        const Issue& issue = issues[i];
        out << (i ? "," : "") << "{\"severity\":\"" << severityName(issue.severity) << "\",\"where\":\""
            << jsonEscaped(issue.where) << "\",\"what\":\"" << jsonEscaped(issue.what)
            << "\",\"repairable\":" << (issue.repairable ? "true" : "false")
            << ",\"repaired\":" << (issue.repaired ? "true" : "false") << "}";
    }
    out << "]}";
    return out.str();
}

void checkContainer(const std::string& path, Report& report)
{
    // An XFL folder named by one of its files (the .xfl marker or
    // DOMDocument.xml) is a folder, not a container.
    const std::string extension = std::filesystem::path(path).extension().string();
    if (extension == ".xfl" || extension == ".xml")
        return;
    // A Flash CS4 (or earlier) document is a compound file, not an
    // archive; reading it is the check.
    {
        std::ifstream file(path, std::ios::binary);
        unsigned char magic[4] = {};
        if (file.read(reinterpret_cast<char*>(magic), 4) && magic[0] == 0xD0 && magic[1] == 0xCF && magic[2] == 0x11 &&
            magic[3] == 0xE0)
            return;
    }
    ZipReader reader;
    if (!reader.open(path))
    {
        add(report, Issue::Severity::Error, path, "not a readable archive: " + reader.errorString());
        return;
    }
    std::set<std::string> files;
    for (const std::string& name : reader.getFileList())
        files.insert(name);
    if (!files.count("DOMDocument.xml"))
    {
        add(report, Issue::Severity::Error, path, "DOMDocument.xml is missing");
        return;
    }
    // Every part the document is built from well-formed. The rest
    // (metadata, publish settings) is carried through as it is, and
    // Animate writes some of it in ways an XML reader balks at.
    for (const std::string& name : files)
    {
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0)
            continue;
        if (name != "DOMDocument.xml" && name.compare(0, 8, "LIBRARY/") != 0)
            continue;
        const std::string text = reader.readTextFile(name);
        tinyxml2::XMLDocument xml;
        if (xml.Parse(text.data(), text.size()) != tinyxml2::XML_SUCCESS)
            add(report, Issue::Severity::Error, name, std::string("damaged XML: ") + xml.ErrorStr());
    }
    // The symbols and bitmaps the document names are there.
    const std::string text = reader.readTextFile("DOMDocument.xml");
    tinyxml2::XMLDocument xml;
    if (xml.Parse(text.data(), text.size()) != tinyxml2::XML_SUCCESS)
        return;
    const tinyxml2::XMLElement* root = xml.RootElement();
    if (const tinyxml2::XMLElement* symbols = root ? root->FirstChildElement("symbols") : nullptr)
    {
        for (const tinyxml2::XMLElement* include = symbols->FirstChildElement("Include"); include;
             include = include->NextSiblingElement("Include"))
        {
            const char* href = include->Attribute("href");
            if (href && !files.count(std::string("LIBRARY/") + href))
                add(report, Issue::Severity::Error, std::string("LIBRARY/") + href, "the symbol's file is missing");
        }
    }
    if (const tinyxml2::XMLElement* media = root ? root->FirstChildElement("media") : nullptr)
    {
        for (const tinyxml2::XMLElement* item = media->FirstChildElement(); item; item = item->NextSiblingElement())
        {
            const char* data = item->Attribute("bitmapDataHRef");
            if (!data)
                data = item->Attribute("soundDataHRef");
            const char* name = item->Attribute("name");
            if (data && !files.count(std::string("bin/") + data))
                add(report, Issue::Severity::Warning, name ? name : data, "its media data (bin/" + std::string(data) + ") is missing");
        }
    }
}

void checkDocument(Document& document, Report& report, bool repair)
{
    SymbolList* list = document.symbolList;

    // Names used twice in the library.
    if (list)
    {
        std::set<std::string> seen;
        for (Symbol* symbol : list->symbols)
        {
            if (!symbol)
                continue;
            if (seen.insert(symbol->name).second)
                continue;
            std::string renamed;
            if (repair)
            {
                for (int n = 2;; ++n)
                {
                    renamed = symbol->name + " " + std::to_string(n);
                    if (!seen.count(renamed) && !list->symbolMap.count(renamed))
                        break;
                }
            }
            add(report, Issue::Severity::Error, symbol->name, "another library item has the same name", true, repair);
            if (repair)
            {
                // Its instances point at it, and follow.
                symbol->name = renamed;
                symbol->href = renamed + ".xml";
                list->symbolMap[renamed] = symbol;
                seen.insert(renamed);
                std::function<void(Timeline*)> rename = [&](Timeline* timeline) {
                    for (Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
                        for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                            if (frame)
                                forEachElement(frame->elements, [&](std::vector<Element*>& elements, size_t i) {
                                    auto* instance = dynamic_cast<SymbolInstance*>(elements[i]);
                                    if (instance && instance->symbol == symbol)
                                        instance->libraryItemName = renamed;
                                    return true;
                                });
                };
                for (Timeline* timeline : document.timelines)
                    rename(timeline);
                for (Symbol* other : list->symbols)
                    for (Timeline* timeline : other ? other->timelines : std::vector<Timeline*>())
                        rename(timeline);
            }
        }
    }

    std::set<std::string> bitmaps;
    for (const Resource* resource : document.resources)
        if (resource && resource->resourceType() == Resource::Type::Bitmap)
            bitmaps.insert(static_cast<const Bitmap*>(resource)->name);
    std::set<std::string> sounds;
    for (const Document::SoundInfo& sound : document.sounds)
        sounds.insert(sound.name);
    std::map<std::string, Symbol*> made;

    const auto checkTimeline = [&](Timeline* timeline, Symbol* owner, const std::string& where) {
        if (!timeline)
            return;
        if (timeline->layers.empty())
        {
            add(report, owner ? Issue::Severity::Warning : Issue::Severity::Error, where, "has no layers", true, repair);
            if (repair)
            {
                Layer* layer = new Layer(timeline);
                layer->name = "Layer_1";
                layer->frames.push_back(new Frame(layer));
                timeline->layers.push_back(layer);
            }
        }
        const int layerCount = static_cast<int>(timeline->layers.size());
        for (int l = 0; l < layerCount; ++l)
        {
            Layer* layer = timeline->layers[static_cast<size_t>(l)];
            if (!layer)
                continue;
            const std::string here = where + " > " + layer->name;
            if (layer->parentLayerIndex >= layerCount || layer->parentLayerIndex == l || layer->parentLayerIndex < -1)
            {
                add(report, Issue::Severity::Error, here, "its folder or mask is a layer that is not there", true, repair);
                if (repair)
                {
                    layer->parentLayerIndex = -1;
                    layer->parentLayer = nullptr;
                }
            }

            // Frames: none missing, in order, each of some length, none
            // over another.
            std::vector<Frame*>& frames = layer->frames;
            const size_t before = frames.size();
            frames.erase(std::remove(frames.begin(), frames.end(), nullptr), frames.end());
            if (frames.size() != before)
                add(report, Issue::Severity::Warning, here, "has empty frame entries", true, true);
            if (!std::is_sorted(frames.begin(), frames.end(), [](const Frame* a, const Frame* b) { return a->index < b->index; }))
            {
                add(report, Issue::Severity::Error, here, "its keyframes are out of order", true, repair);
                if (repair)
                    std::stable_sort(frames.begin(), frames.end(), [](const Frame* a, const Frame* b) { return a->index < b->index; });
            }
            for (size_t i = 0; i < frames.size();)
            {
                Frame* frame = frames[i];
                const std::string at = here + " > frame " + std::to_string(frame->index + 1);
                if (frame->index < 0)
                {
                    add(report, Issue::Severity::Error, at, "starts before the first frame", true, repair);
                    if (repair)
                        frame->index = 0;
                }
                if (frame->duration < 1)
                {
                    add(report, Issue::Severity::Error, at, "lasts no frames", true, repair);
                    if (repair)
                        frame->duration = 1;
                }
                if (i > 0)
                {
                    Frame* previous = frames[i - 1];
                    const int end = previous->index + previous->duration;
                    if (frame->index == previous->index)
                    {
                        add(report, Issue::Severity::Error, at, "two keyframes start on the same frame; the second is dropped",
                            true, repair);
                        if (repair)
                        {
                            delete frame;
                            frames.erase(frames.begin() + static_cast<long>(i));
                            continue;
                        }
                    }
                    else if (frame->index < end)
                    {
                        add(report, Issue::Severity::Error, at, "starts inside the span before it", true, repair);
                        if (repair)
                            previous->duration = frame->index - previous->index;
                    }
                    else if (frame->index > end)
                    {
                        add(report, Issue::Severity::Warning, at, "a gap of no frames before it", true, repair);
                        if (repair)
                            previous->duration = frame->index - previous->index;
                    }
                }
                ++i;
            }

            for (Frame* frame : frames)
            {
                const std::string at = here + " > frame " + std::to_string(frame->index + 1);
                for (auto it = frame->extraAttributes.begin(); it != frame->extraAttributes.end();)
                {
                    if (it->first == "soundName" && !sounds.count(it->second))
                    {
                        add(report, Issue::Severity::Warning, at, "plays the sound " + it->second + ", which is not in the library",
                            true, repair);
                        if (repair)
                        {
                            it = frame->extraAttributes.erase(it);
                            continue;
                        }
                    }
                    ++it;
                }
                forEachElement(frame->elements, [&](std::vector<Element*>& elements, size_t i) {
                    Element* element = elements[i];
                    if (!element)
                    {
                        elements.erase(elements.begin() + static_cast<long>(i));
                        return false;
                    }
                    if (element->elementType() == Element::Type::SymbolInstance)
                    {
                        auto* instance = static_cast<SymbolInstance*>(element);
                        if (owner && instance->symbol == owner)
                        {
                            add(report, Issue::Severity::Error, at, "the symbol holds an instance of itself", true, repair);
                            if (repair)
                            {
                                delete element;
                                elements.erase(elements.begin() + static_cast<long>(i));
                                return false;
                            }
                        }
                        else if (!instance->symbol)
                        {
                            add(report, Issue::Severity::Error, at,
                                "an instance of " + instance->libraryItemName + ", which is not in the library; an empty symbol stands in",
                                true, repair);
                            if (repair && list)
                            {
                                Symbol*& standIn = made[instance->libraryItemName];
                                if (!standIn)
                                {
                                    standIn = list->symbolMap.count(instance->libraryItemName)
                                        ? list->symbolMap[instance->libraryItemName] : nullptr;
                                }
                                if (!standIn)
                                {
                                    standIn = new Symbol(list);
                                    standIn->name = instance->libraryItemName;
                                    standIn->href = standIn->name + ".xml";
                                    standIn->symbolType = SymbolType::Graphic;
                                    Timeline* inner = new Timeline(standIn);
                                    inner->name = standIn->name;
                                    Layer* innerLayer = new Layer(inner);
                                    innerLayer->name = "Layer_1";
                                    innerLayer->frames.push_back(new Frame(innerLayer));
                                    inner->layers.push_back(innerLayer);
                                    standIn->timelines.push_back(inner);
                                    list->symbols.push_back(standIn);
                                    list->symbolMap[standIn->name] = standIn;
                                }
                                instance->symbol = standIn;
                            }
                        }
                    }
                    else if (element->elementType() == Element::Type::BitmapInstance)
                    {
                        auto* bitmap = static_cast<BitmapInstance*>(element);
                        // Animate keeps such an instance (it draws nothing),
                        // so it is reported and left.
                        if (!bitmaps.count(bitmap->libraryItemName))
                            add(report, Issue::Severity::Warning, at,
                                "an instance of the bitmap " + bitmap->libraryItemName + ", which is not in the library");
                    }
                    else if (element->elementType() == Element::Type::Shape)
                    {
                        Shape* shape = static_cast<Shape*>(element);
                        const auto checkFill = [&](const FillStyle* fill) {
                            if (fill && fill->type() == FillStyle::Type::BitmapFill &&
                                !bitmaps.count(static_cast<const BitmapFill*>(fill)->bitmapPath))
                                add(report, Issue::Severity::Warning, at,
                                    "a fill uses the bitmap " + static_cast<const BitmapFill*>(fill)->bitmapPath +
                                        ", which is not in the library");
                        };
                        for (const FillStyle* fill : shape->fills)
                            checkFill(fill);
                        for (const StrokeStyle* stroke : shape->strokes)
                            if (stroke)
                                checkFill(stroke->fill);
                    }
                    return true;
                });
            }
        }
    };

    for (size_t i = 0; i < document.timelines.size(); ++i)
    {
        Timeline* scene = document.timelines[i];
        checkTimeline(scene, nullptr, scene && !scene->name.empty() ? scene->name : "Scene " + std::to_string(i + 1));
    }
    if (document.timelines.empty())
        add(report, Issue::Severity::Error, "Document", "has no scenes");
    if (list)
    {
        // Stand-ins made while checking need no checking themselves.
        const std::vector<Symbol*> symbols = list->symbols;
        for (Symbol* symbol : symbols)
        {
            if (!symbol)
                continue;
            if (symbol->timelines.empty())
            {
                add(report, Issue::Severity::Warning, symbol->name, "the symbol has no timeline", true, repair);
                if (repair)
                {
                    Timeline* inner = new Timeline(symbol);
                    inner->name = symbol->name;
                    symbol->timelines.push_back(inner);
                }
            }
            for (Timeline* timeline : symbol->timelines)
                checkTimeline(timeline, symbol, symbol->name);
        }
    }
}

bool salvage(const std::string& path, const std::string& folder, std::vector<std::string>& recovered,
    std::vector<std::string>& notes, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "Cannot read " + path;
        return false;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto u16 = [&data](size_t at) { return static_cast<uint32_t>(data[at] | data[at + 1] << 8); };
    const auto u32 = [&data](size_t at) {
        return static_cast<uint32_t>(data[at] | data[at + 1] << 8 | data[at + 2] << 16) | static_cast<uint32_t>(data[at + 3]) << 24;
    };
    namespace fs = std::filesystem;
    std::error_code ignored;
    fs::create_directories(fs::u8path(folder), ignored);

    // Walk the local file headers in order. A header's sizes may be zero
    // when a data descriptor follows the data: then the deflate stream is
    // read to its end.
    for (size_t at = 0; at + 30 <= data.size();)
    {
        if (u32(at) != 0x04034b50)
        {
            ++at;
            continue;
        }
        const uint32_t method = u16(at + 8);
        uint32_t compressed = u32(at + 18);
        const uint32_t nameLength = u16(at + 26);
        const uint32_t extraLength = u16(at + 28);
        const size_t start = at + 30 + nameLength + extraLength;
        if (start > data.size())
            break;
        const std::string name(reinterpret_cast<const char*>(&data[at + 30]), nameLength);
        std::vector<uint8_t> out;
        bool ok = false;
        if (method == 0)
        {
            if (compressed > 0 && start + compressed <= data.size())
            {
                out.assign(data.begin() + static_cast<long>(start), data.begin() + static_cast<long>(start + compressed));
                ok = true;
            }
        }
        else if (method == 8)
        {
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) == Z_OK)
            {
                stream.next_in = const_cast<Bytef*>(&data[start]);
                stream.avail_in = static_cast<uInt>(data.size() - start);
                uint8_t buffer[65536];
                int status = Z_OK;
                while (status == Z_OK)
                {
                    stream.next_out = buffer;
                    stream.avail_out = sizeof(buffer);
                    status = inflate(&stream, Z_NO_FLUSH);
                    out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
                }
                // A part cut off by the end of the file is kept as far as
                // it goes: a truncated XML can still be read in part.
                ok = status == Z_STREAM_END || !out.empty();
                if (compressed == 0)
                    compressed = static_cast<uint32_t>(stream.total_in);
                inflateEnd(&stream);
            }
        }
        if (ok && !name.empty() && name.back() != '/')
        {
            const fs::path target = fs::u8path(folder) / fs::u8path(name);
            fs::create_directories(target.parent_path(), ignored);
            std::ofstream file(target, std::ios::binary);
            file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
            recovered.push_back(name);
        }
        at = start + std::max<uint32_t>(compressed, 1);
    }
    if (recovered.empty())
    {
        error = "Nothing could be read from " + path;
        return false;
    }

    // A symbol whose file was cut off, or never reached, would stop the
    // whole document from opening: an empty symbol of its name stands in.
    const fs::path root = fs::u8path(folder);
    std::ifstream documentFile(root / "DOMDocument.xml", std::ios::binary);
    const std::string documentText((std::istreambuf_iterator<char>(documentFile)), std::istreambuf_iterator<char>());
    tinyxml2::XMLDocument xml;
    if (documentText.empty() || xml.Parse(documentText.data(), documentText.size()) != tinyxml2::XML_SUCCESS)
        return true;
    const tinyxml2::XMLElement* symbols = xml.RootElement() ? xml.RootElement()->FirstChildElement("symbols") : nullptr;
    for (const tinyxml2::XMLElement* include = symbols ? symbols->FirstChildElement("Include") : nullptr; include;
         include = include->NextSiblingElement("Include"))
    {
        const char* href = include->Attribute("href");
        if (!href)
            continue;
        const fs::path file = root / "LIBRARY" / fs::u8path(href);
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        tinyxml2::XMLDocument symbol;
        if (!text.empty() && symbol.Parse(text.data(), text.size()) == tinyxml2::XML_SUCCESS)
            continue;
        std::string name = href;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".xml") == 0)
            name.resize(name.size() - 4);
        tinyxml2::XMLDocument stub;
        tinyxml2::XMLElement* item = stub.NewElement("DOMSymbolItem");
        item->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
        item->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
        item->SetAttribute("name", name.c_str());
        item->SetAttribute("symbolType", "graphic");
        stub.InsertEndChild(item);
        tinyxml2::XMLElement* timeline = item->InsertNewChildElement("timeline")->InsertNewChildElement("DOMTimeline");
        timeline->SetAttribute("name", name.c_str());
        tinyxml2::XMLElement* layer = timeline->InsertNewChildElement("layers")->InsertNewChildElement("DOMLayer");
        layer->SetAttribute("name", "Layer_1");
        tinyxml2::XMLElement* frame = layer->InsertNewChildElement("frames")->InsertNewChildElement("DOMFrame");
        frame->SetAttribute("index", 0);
        frame->InsertNewChildElement("elements");
        fs::create_directories(file.parent_path(), ignored);
        stub.SaveFile(file.u8string().c_str());
        notes.push_back("LIBRARY/" + std::string(href) + (text.empty() ? " was missing" : " was damaged") +
                        "; an empty symbol stands in for " + name);
    }
    return true;
}

} // namespace doctor

} // namespace fla
