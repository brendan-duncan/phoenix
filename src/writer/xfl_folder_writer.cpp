#include "xfl_folder_writer.h"

#include "xfl_content.h"

#include "../data/document.h"

#include <filesystem>
#include <fstream>

namespace fla {

namespace fs = std::filesystem;

bool XFLFolderWriter::writeFile(const std::string& path, const void* data, size_t size)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        _errorString = "Failed to open for writing: " + path;
        return false;
    }

    if (size > 0)
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));

    if (!stream)
    {
        _errorString = "Failed to write: " + path;
        return false;
    }

    return true;
}

bool XFLFolderWriter::write(const Document& document, const std::string& directory,
    const std::string& projectName)
{
    _errorString.clear();
    _unsupported.clear();

    const fs::path root(directory);

    std::string name = projectName;
    if (name.empty())
        name = root.filename().string();
    if (name.empty())
        name = "Untitled";

    std::vector<XFLEntry> entries;
    if (!XFLContent::build(document, name, entries, _unsupported, _errorString))
        return false;

    std::error_code error;
    fs::create_directories(root, error);
    if (error)
    {
        _errorString = "Failed to create directory " + directory + ": " + error.message();
        return false;
    }

    for (const XFLEntry& entry : entries)
    {
        const fs::path path = root / fs::path(entry.path);

        fs::create_directories(path.parent_path(), error);
        if (error)
        {
            _errorString = "Failed to create " + path.parent_path().string() + ": " + error.message();
            return false;
        }

        const bool ok = entry.isBinary()
            ? writeFile(path.string(), entry.binary->data(), entry.binary->size())
            : writeFile(path.string(), entry.text.data(), entry.text.size());

        if (!ok)
            return false;
    }

    return true;
}

} // namespace fla
