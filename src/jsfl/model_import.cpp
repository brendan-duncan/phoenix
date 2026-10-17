// document.importFile and document.importEmbeddedSWF. Images, sounds and
// vector artwork go to the host, which has their decoders; documents and
// movies (FLA, XFL, SWF) are read here: their library comes in, and their
// main timeline as a movie clip named after the file, placed on the
// current frame unless the import is to the library.

#include "extensions.h"
#include "flfile.h"
#include "model.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../edit/document_import.h"
#include "../edit/element_commands.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace jsfl {

namespace {

/// Brings in the document or movie at \a path, as importFile does; the
/// main timeline's symbol, or null when nothing came in.
fla::Symbol* importDocument(Document& d, const std::string& path, bool toLibrary, const std::string& symbolName)
{
    fla::Document& data = d.data();
    fla::documentImport::Result result;
    std::string error;
    if (!fla::documentImport::read(data, path, result, error, symbolName))
    {
        d.session.host().trace(error);
        return nullptr;
    }
    // What could not be brought across exactly, said once.
    if (!result.notes.empty())
        d.session.host().trace(result.notes);
    d.push(std::move(result.command));
    fla::Symbol* symbol = nullptr;
    if (!result.symbol.empty() && data.symbolList)
    {
        const auto found = data.symbolList->symbolMap.find(result.symbol);
        symbol = found == data.symbolList->symbolMap.end() ? nullptr : found->second;
    }
    if (toLibrary || !symbol)
        return symbol;
    // On the stage where the movie's own stage was: at the origin.
    fla::Frame* frame = currentKeyframe(d.session, d.document);
    auto* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = symbol->name;
    instance->symbol = symbol;
    instance->symbolType = symbol->symbolType;
    instance->loopType = fla::LoopType::Loop;
    fla::Selection& selection = d.session.host().selection(d.document);
    d.push(fla::CommandPtr(new fla::AddElementCommand(frame, instance, "Import", &selection)));
    selection.select(instance);
    return symbol;
}

/// A file name in the system's temporary folder no other import has.
std::filesystem::path temporarySwf()
{
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::error_code ec;
    return std::filesystem::temp_directory_path(ec) /
           ("phoenix-embedded-" + std::to_string(stamp) + "-" + std::to_string(++counter) + ".swf");
}

} // namespace

void describeDocumentImport(ClassDef<Document>& def)
{
    def.method("importFile", 4, [](Document& d, const Args& args) {
        d.data();
        const std::string path = uri::toPath(arg(args, 0).toString());
        std::error_code missing;
        if (path.empty() || !std::filesystem::is_regular_file(std::filesystem::u8path(path), missing))
            return Value(false);
        if (d.document != d.session.host().activeDocument())
        {
            d.session.beforeSwitch();
            d.session.host().activate(d.document);
        }
        // What the import pushes is part of the script's undo step.
        d.session.beginStep(d.document);
        if (fla::documentImport::handles(path))
        {
            const bool imported = importDocument(d, path, arg(args, 1).toBool(), std::string()) != nullptr;
            d.session.invalidate(d.document);
            return Value(imported);
        }
        std::string error;
        const bool imported = d.session.host().importFile(d.document, path, arg(args, 1).toBool(), error);
        d.session.invalidate(d.document);
        if (!imported && !error.empty())
            d.session.host().trace(error);
        return Value(imported);
    });
    // importEmbeddedSWF(linkageName, swfData, libName): an SWF movie held
    // in memory (an array of its bytes, as a native library hands it over)
    // into the library as a movie clip, exported for ActionScript under
    // the linkage name.
    def.method("importEmbeddedSWF", 3, [](Document& d, const Args& args) {
        d.data();
        const std::string linkage = arg(args, 0).toString();
        const Value& bytes = arg(args, 1);
        std::string swf;
        if (bytes.type == Value::Type::Array)
        {
            swf.reserve(bytes.items.size());
            for (const Value& byte : bytes.items)
                swf.push_back(static_cast<char>(byte.toInt() & 0xFF));
        }
        else if (bytes.type == Value::Type::String)
            swf = bytes.string;
        if (swf.size() < 8)
            throw Error("importEmbeddedSWF needs the movie's bytes, as an array of numbers.");
        const std::filesystem::path file = temporarySwf();
        {
            std::ofstream out(file, std::ios::binary);
            out.write(swf.data(), static_cast<std::streamsize>(swf.size()));
            if (!out)
                throw Error("The movie could not be written to a temporary file.");
        }
        if (d.document != d.session.host().activeDocument())
        {
            d.session.beforeSwitch();
            d.session.host().activate(d.document);
        }
        d.session.beginStep(d.document);
        const std::string name = given(args, 2) && !arg(args, 2).toString().empty() ? arg(args, 2).toString()
                                 : linkage.empty()                                  ? std::string("Embedded SWF")
                                                                                    : linkage;
        fla::Symbol* symbol = importDocument(d, file.u8string(), true, name);
        std::error_code ec;
        std::filesystem::remove(file, ec);
        if (!symbol)
            throw Error("The movie could not be read.");
        if (!linkage.empty())
        {
            std::vector<std::pair<std::string, std::string>> after = symbol->extraAttributes;
            const auto set = [&after](const std::string& key, const std::string& value) {
                for (auto& entry : after)
                {
                    if (entry.first == key)
                    {
                        entry.second = value;
                        return;
                    }
                }
                after.emplace_back(key, value);
            };
            set("linkageExportForAS", "true");
            set("linkageExportInFirstFrame", "true");
            set("linkageIdentifier", linkage);
            set("linkageClassName", linkage);
            d.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                       symbol->extraAttributes, symbol->extraAttributes, after, "Linkage")),
                false);
        }
        d.session.invalidate(d.document);
        return Value();
    });
}

} // namespace jsfl
