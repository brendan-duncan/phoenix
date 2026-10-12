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

    // Animate recognizes a project by this marker, whose text is always
    // PROXY-CS5. Callers can leave the name empty to skip it.
    if (!projectName.empty())
    {
        XFLEntry marker;
        marker.path = projectName + ".xfl";
        marker.text = "PROXY-CS5";
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

        if (bitmap->imageData.empty() && bitmap->datData.empty())
        {
            // Writing an empty file would look like a valid but blank image, so
            // leave it out and say so.
            unsupported.push_back("bitmap '" + bitmap->name + "' has no image data");
            continue;
        }

        if (!bitmap->imageData.empty())
        {
            XFLEntry entry;
            entry.path = "LIBRARY/" + bitmap->href;
            entry.binary = &bitmap->imageData;
            entries.push_back(std::move(entry));
        }

        if (!bitmap->datData.empty() && !bitmap->bitmapDataHRef.empty() &&
            isSafeRelativePath(bitmap->bitmapDataHRef))
        {
            XFLEntry entry;
            entry.path = "bin/" + bitmap->bitmapDataHRef;
            entry.binary = &bitmap->datData;
            entries.push_back(std::move(entry));
        }
    }

    // Files the document carried through without modeling: sound data,
    // the brush definitions file.
    for (const auto& file : document.extraFiles)
    {
        if (!isSafeRelativePath(file.first))
            continue;
        XFLEntry entry;
        entry.path = file.first;
        entry.binary = &file.second;
        entries.push_back(std::move(entry));
    }

    // The files every Animate package has, for a document that came without
    // them (one made here): the media type and an empty metadata file. A
    // document read from a file carries its own through extraFiles.
    const auto carried = [&document](const std::string& path) {
        for (const auto& file : document.extraFiles)
        {
            if (file.first == path)
                return true;
        }
        return false;
    };
    if (!carried("META-INF/metadata.xml"))
    {
        XFLEntry metadata;
        metadata.path = "META-INF/metadata.xml";
        entries.push_back(std::move(metadata));
    }
    if (!carried("mimetype"))
    {
        XFLEntry mimetype;
        mimetype.path = "mimetype";
        mimetype.text = "application/vnd.adobe.xfl";
        entries.push_back(std::move(mimetype));
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
