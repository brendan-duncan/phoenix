#include "fla_writer.h"

#include "xfl_content.h"

#include "../data/document.h"
#include "../util/zip_writer.h"

namespace fla {

bool FLAWriter::write(const Document& document, const std::string& filePath)
{
    _errorString.clear();
    _unsupported.clear();

    // A zipped .fla carries no .xfl marker; that file only exists to mark a
    // folder as a project.
    std::vector<XFLEntry> entries;
    if (!XFLContent::build(document, std::string(), entries, _unsupported, _errorString))
        return false;

    ZipWriter zip;
    if (!zip.open(filePath))
    {
        _errorString = zip.errorString();
        return false;
    }

    for (const XFLEntry& entry : entries)
    {
        const bool ok = entry.isBinary()
            ? zip.addFile(entry.path, *entry.binary)
            : zip.addFile(entry.path, entry.text);

        if (!ok)
        {
            _errorString = zip.errorString();
            return false;
        }
    }

    if (!zip.close())
    {
        _errorString = zip.errorString();
        return false;
    }

    return true;
}

} // namespace fla
