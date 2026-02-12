#include "zip_reader.h"
#include "../util/zip_archive.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

ZipReader::ZipReader()
    : _zipArchive(nullptr)
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
    _zipArchive = new ZipArchive();
    if (!_zipArchive->open(filePath.string()))
    {
        _errorString = "Failed to open zip file: " + filePath.string();
        delete _zipArchive;
        _zipArchive = nullptr;
        return false;
    }

    _path = filePath.string();
    _isOpen = true;

    return true;
}

void ZipReader::close()
{
    if (_zipArchive)
    {
        delete _zipArchive;
        _zipArchive = nullptr;
    }
    _isOpen = false;
    _isDirectory = false;
}

bool ZipReader::isOpen() const
{
    return _isOpen && (_zipArchive || _isDirectory);
}

std::vector<std::string> ZipReader::getFileList() const
{
    std::vector<std::string> fileList;

    if (!isOpen())
    {
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

    if (_isDirectory)
    {
        // Read from filesystem
        fs::path filePath = fs::path(_path) / fileName;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return std::vector<uint8_t>();
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        {
            return std::vector<uint8_t>();
        }

        return buffer;
    }

    std::vector<uint8_t> buffer;
    if (!_zipArchive->extractFile(fileName, buffer))
    {
        return std::vector<uint8_t>();
    }

    return buffer;
}

std::string ZipReader::readTextFile(const std::string& fileName) const
{
    std::vector<uint8_t> data = readFile(fileName);
    return std::string(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
}

bool ZipReader::containsFile(const std::string& fileName) const
{
    if (!isOpen())
        return false;

    if (_isDirectory)
    {
        fs::path filePath = fs::path(_path) / fileName;
        return fs::exists(filePath) && fs::is_regular_file(filePath);
    }

    // Check if file exists in zip archive
    for (const auto& entry : _zipArchive->getEntries())
    {
        if (entry.filename == fileName)
        {
            return true;
        }
    }
    return false;
}

std::string ZipReader::errorString() const
{
    return _errorString;
}
