#pragma once

#include <string>
#include <vector>

class QZipReader;

class ZipReader
{
public:
    ZipReader();

    ~ZipReader();

    // Open a zip file
    bool open(const std::string& zipFilePath);

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

private:
    QZipReader* _zipReader;
    std::string _path;
    std::string _errorString;
    bool _isOpen;
    bool _isDirectory;
};
