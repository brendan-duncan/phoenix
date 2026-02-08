#include "zip_reader.h"

#include <QFile>
#include <QtCore/private/qzipreader_p.h>

ZipReader::ZipReader()
    : _zipReader(nullptr)
    , _isOpen(false)
{}

ZipReader::~ZipReader()
{
    close();
}

bool ZipReader::open(const std::string& zipFilePath)
{
    close();

    if (!QFile::exists(QString::fromStdString(zipFilePath)))
    {
        _errorString =  "Zip file does not exist: " + zipFilePath;
        return false;
    }

    try
    {
        // Create QZipReader instance
        _zipReader = new QZipReader(QString::fromStdString(zipFilePath));

        // Check if the zip file is valid
        if (_zipReader->status() != QZipReader::NoError)
        {
            _errorString = "Invalid zip file: " + zipFilePath + " (status: " + std::to_string(_zipReader->status()) + ")";

            delete _zipReader;
            _zipReader = nullptr;
            return false;
        }

        _isOpen = true;
        return true;
    }
    catch (const std::exception& e)
    {
        _errorString = "Failed to open zip file: " + std::string(e.what());
        _zipReader = nullptr;
        return false;
    }
    catch (...)
    {
        _errorString = "Unknown error occurred while opening zip file";
        _zipReader = nullptr;
        return false;
    }
}

void ZipReader::close()
{
    if (_zipReader)
    {
        delete _zipReader;
        _zipReader = nullptr;
    }
    _isOpen = false;
}

bool ZipReader::isOpen() const
{
    return _isOpen && _zipReader;
}

std::vector<std::string> ZipReader::getFileList() const
{
    std::vector<std::string> fileList;

    if (!isOpen())
    {
        return fileList;
    }

    try
    {
        QList<QZipReader::FileInfo> fileInfoList = _zipReader->fileInfoList();

        for (const QZipReader::FileInfo& fileInfo : fileInfoList)
        {
            // Skip directories (entries ending with '/')
            if (!fileInfo.filePath.endsWith("/"))
            {
                fileList.push_back(fileInfo.filePath.toStdString());
            }
        }

        return fileList;

    }
    catch (const std::exception& e)
    {
        return fileList;
    }
    catch (...)
    {
        return fileList;
    }
}

std::vector<uint8_t> ZipReader::readFile(const std::string& fileName) const
{
    if (!isOpen())
    {
        return std::vector<uint8_t>();
    }

    try
    {
        QByteArray data = _zipReader->fileData(QString::fromStdString(fileName));
        return std::vector<uint8_t>(data.begin(), data.end());

    }
    catch (const std::exception& e)
    {
        return std::vector<uint8_t>();
    }
    catch (...)
    {
        return std::vector<uint8_t>();
    }
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

    try
    {
        QList<QZipReader::FileInfo> fileInfoList = _zipReader->fileInfoList();

        for (const QZipReader::FileInfo& fileInfo : fileInfoList)
        {
            if (fileInfo.filePath == QString::fromStdString(fileName))
            {
                return true;
            }
        }

        return false;

    }
    catch (...)
    {
        return false;
    }
}

std::string ZipReader::errorString() const
{
    return _errorString;
}
