#include "zip_reader.h"
#include "../util/zip_archive.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

ZipReader::ZipReader()
    : _zipArchive()
    , _isOpen(false)
    , _isDirectory(false)
{}

ZipReader::~ZipReader()
{
    close();
}

bool ZipReader::open(const std::string& zipFilePath)
{
    close();

    fs::path filePath(zipFilePath);

    // Handle .xml files by using their parent directory
    if (filePath.extension() == ".xml")
    {
        if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
        {
            _errorString = "XML file does not exist: " + zipFilePath;
            return false;
        }
        // Use the directory containing the XML file as the path
        filePath = filePath.parent_path();
        std::cout << "Opening XML file directly, using directory: " << filePath.string() << std::endl;
    }

    if (!fs::exists(filePath))
    {
        _errorString = "Zip file does not exist: " + filePath.string();
        return false;
    }

    _isDirectory = fs::is_directory(filePath);

    if (_isDirectory)
    {
        fs::path domDocPath = filePath / "DOMDocument.xml";
        if (!fs::exists(domDocPath))
        {
            _errorString = "Specified path is a directory but does not contain DOMDocument.xml: " + filePath.string();
            return false;
        }

        _path = filePath.string();
        _isOpen = true;
        return true;
    }

    // Open zip file using libzip
    int error = 0;
    _zipArchive = std::make_shared<ZipArchive>();
    if (!_zipArchive->open(filePath.string()))
    {
        _errorString = "Failed to open zip file: " + filePath.string();
        _zipArchive.reset();
        return false;
    }

    _path = filePath.string();
    _isOpen = true;

    return true;
}

void ZipReader::openMemory(std::map<std::string, std::vector<uint8_t>> files)
{
    close();
    _memory = std::move(files);
    _isMemory = true;
    _isOpen = true;
}

void ZipReader::close()
{
    _zipArchive.reset();
    _isOpen = false;
    _isDirectory = false;
    _isMemory = false;
    _memory.clear();
}

bool ZipReader::isOpen() const
{
    return _isOpen && (_zipArchive || _isDirectory || _isMemory);
}

std::vector<std::string> ZipReader::getFileList() const
{
    std::vector<std::string> fileList;

    if (!isOpen())
    {
        return fileList;
    }

    if (_isMemory)
    {
        for (const auto& entry : _memory)
            fileList.push_back(entry.first);
        return fileList;
    }

    if (_isDirectory)
    {
        // If it's a directory, return the list of files in that directory
        try
        {
            for (const auto& entry : fs::directory_iterator(_path))
            {
                if (fs::is_regular_file(entry))
                {
                    fileList.push_back(entry.path().filename().string());
                }
            }
        }
        catch (const std::exception& e)
        {
            // Return empty list on error
        }
        return fileList;
    }

    for (const auto& entry : _zipArchive->getEntries())
    {
        // Skip directories (entries ending with '/')
        if (!entry.filename.empty() && entry.filename.back() != '/')
        {
            fileList.push_back(entry.filename);
        }
    }

    return fileList;
}

std::vector<uint8_t> ZipReader::readFile(const std::string& fileName) const
{
    if (!isOpen())
    {
        return std::vector<uint8_t>();
    }

    if (_isMemory)
    {
        const auto found = _memory.find(fileName);
        return found != _memory.end() ? found->second : std::vector<uint8_t>();
    }

    if (_isDirectory)
    {
        // Read from filesystem
        if (!withinFolder(fileName))
        {
            return std::vector<uint8_t>();
        }
        fs::path filePath = fs::path(_path) / fileName;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return std::vector<uint8_t>();
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0)
        {
            return std::vector<uint8_t>();
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        {
            return std::vector<uint8_t>();
        }

        return buffer;
    }

    std::vector<uint8_t> buffer;
    if (!_zipArchive->extractFile(_root + fileName, buffer))
    {
        return std::vector<uint8_t>();
    }

    return buffer;
}

std::string ZipReader::readTextFile(const std::string& fileName) const
{
    std::vector<uint8_t> data = readFile(fileName);
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

bool ZipReader::withinFolder(const std::string& fileName)
{
    // A name from the document (an href) may not climb out of its folder.
    const fs::path name(fileName);
    if (name.empty() || name.has_root_name() || name.has_root_directory())
        return false;
    for (const fs::path& part : name)
    {
        if (part == "..")
            return false;
    }
    return true;
}

bool ZipReader::containsFile(const std::string& fileName) const
{
    if (!isOpen())
        return false;

    if (_isMemory)
        return _memory.count(fileName) != 0;

    if (_isDirectory)
    {
        if (!withinFolder(fileName))
            return false;
        fs::path filePath = fs::path(_path) / fileName;
        std::error_code ignored;
        return fs::is_regular_file(filePath, ignored);
    }

    return _zipArchive->find(_root + fileName) != nullptr;
}

fla::LazyBytes ZipReader::lazyFile(const std::string& fileName) const
{
    if (!isOpen() || _isMemory || _isDirectory || !_zipArchive)
        return fla::LazyBytes(readFile(fileName));
    const std::string name = _root + fileName;
    const ZipFileEntry* entry = _zipArchive->find(name);
    if (!entry)
        return fla::LazyBytes();
    auto location = std::make_shared<fla::ByteLocation>();
    location->path = _path;
    location->size = entry->uncompressed_size;
    std::shared_ptr<ZipArchive> archive = _zipArchive;
    location->load = [archive, name](const fla::ByteLocation&, std::vector<uint8_t>& out) {
        return archive->extractFile(name, out);
    };
    return fla::LazyBytes(location);
}

std::string ZipReader::errorString() const
{
    return _errorString;
}
