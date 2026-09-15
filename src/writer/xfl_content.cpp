#include "xfl_content.h"

#include "xfl_writer.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"

#include <filesystem>

namespace fla {

namespace fs = std::filesystem;

bool XFLContent::isSafeRelativePath(const std::string& path)
{
    if (path.empty())
        return false;

    const fs::path candidate(path);
    if (candidate.is_absolute() || candidate.has_root_name())
        return false;

    for (const fs::path& part : candidate)
    {
        if (part == "..")
            return false;
    }

    return true;
}

bool XFLContent::build(const Document& document, const std::string& projectName,
    std::vector<XFLEntry>& entries, std::vector<std::string>& unsupported,
    std::string& error)
{
    entries.clear();
    unsupported.clear();
    error.clear();

    XFLWriter writer;

    XFLEntry documentEntry;
    documentEntry.path = "DOMDocument.xml";
    documentEntry.text = writer.writeDocument(document);
    entries.push_back(std::move(documentEntry));

    // Animate recognises a folder as a project by this marker. A zipped .fla
    // does not need one, so callers can leave the name empty to skip it.
    if (!projectName.empty())
    {
        XFLEntry marker;
        marker.path = projectName + ".xfl";
        marker.text = "PROJECT_XFL\n";
        entries.push_back(std::move(marker));
    }

    if (document.symbolList)
    {
        for (const Symbol* symbol : document.symbolList->symbols)
        {
            if (!symbol)
                continue;

            const std::string href = XFLWriter::symbolHref(*symbol);
            if (!isSafeRelativePath(href))
            {
                error = "Symbol '" + symbol->name + "' has an unusable library path: " + href;
                return false;
            }

            XFLEntry entry;
            entry.path = "LIBRARY/" + href;
            entry.text = writer.writeSymbol(*symbol);
            entries.push_back(std::move(entry));
        }
    }

    for (const Resource* resource : document.resources)
    {
        if (!resource || resource->resourceType() != Resource::Type::Bitmap)
            continue;

        const Bitmap* bitmap = static_cast<const Bitmap*>(resource);
        if (!isSafeRelativePath(bitmap->href))
        {
            error = "Bitmap '" + bitmap->name + "' has an unusable library path: " + bitmap->href;
            return false;
        }

        if (bitmap->imageData.empty())
        {
            // Writing an empty file would look like a valid but blank image, so
            // leave it out and say so.
            unsupported.push_back("bitmap '" + bitmap->name + "' has no image data");
            continue;
        }

        XFLEntry entry;
        entry.path = "LIBRARY/" + bitmap->href;
        entry.binary = &bitmap->imageData;
        entries.push_back(std::move(entry));
    }

    for (const std::string& description : writer.unsupported())
        unsupported.push_back(description);

    return true;
}

std::vector<std::string> unsupportedContent(const Document& document)
{
    std::vector<XFLEntry> entries;
    std::vector<std::string> unsupported;
    std::string error;

    if (!XFLContent::build(document, std::string(), entries, unsupported, error))
        unsupported.push_back(error);

    return unsupported;
}

} // namespace fla
