#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ZipArchive;

class ZipReader
{
public:
    ZipReader();

    ~ZipReader();

    // Open a zip file
    bool open(const std::string& zipFilePath);

    /// Serves the files given, by name, as though read from an archive:
    /// a document converted in memory, as an old binary FLA is.
    void openMemory(std::map<std::string, std::vector<uint8_t>> files);

    // Close the zip file
    void close();

    // Check if zip is open
    bool isOpen() const;

    // Get list of files in zip
    std::vector<std::string> getFileList() const;

    // Read a file from zip as byte array
    std::vector<uint8_t> readFile(const std::string& fileName) const;

    // Read a file from zip as string
    std::string readTextFile(const std::string& fileName) const;

    // Check if a file exists in zip
    bool containsFile(const std::string& fileName) const;

    // Get error message
    std::string errorString() const;

    /// Reads everything from under \a prefix ("folder/"), for an archive
    /// that holds a document folder rather than the document itself.
    void setRoot(const std::string& prefix) { _root = prefix; }

private:
    ZipArchive* _zipArchive;
    std::string _path;
    std::string _errorString;
    bool _isOpen;
    bool _isDirectory;
    bool _isMemory = false;
    std::map<std::string, std::vector<uint8_t>> _memory;
    std::string _root;
};
