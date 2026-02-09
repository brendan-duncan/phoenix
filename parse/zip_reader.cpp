#include "zip_reader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtCore/private/qzipreader_p.h>
#include <QDebug>

ZipReader::ZipReader()
    : _zipReader(nullptr)
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

    QString filePath = QString::fromStdString(zipFilePath);
    if (filePath.endsWith(".xml"))
    {
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile())
        {
            _errorString = "XML file does not exist: " + zipFilePath;
            return false;
        }
        // Use the directory containing the XML file as the path
        filePath = fileInfo.path();
        qDebug() << "Opening XML file directly, using directory:" << filePath;
    }

    if (!QFile::exists(filePath))
    {
        _errorString =  "Zip file does not exist: " + filePath.toStdString();
        return false;
    }

    QFileInfo fileInfo(filePath);
    _isDirectory = fileInfo.isDir();

    if (_isDirectory)
    {
        if (!QFile::exists(filePath + "/DOMDocument.xml"))
        {
            _errorString = "Specified path is a directory but does not contain DOMDocument.xml: " + filePath.toStdString();
            return false;
        }

        _path = filePath.toStdString();
        _isOpen = true;
        return true;
    }

    try
    {
        // Create QZipReader instance
        _zipReader = new QZipReader(filePath);

        // Check if the zip file is valid
        if (_zipReader->status() != QZipReader::NoError)
        {
            _errorString = "Invalid zip file: " + filePath.toStdString() + " (status: " + std::to_string(_zipReader->status()) + ")";

            delete _zipReader;
            _zipReader = nullptr;
            return false;
        }

        _path = filePath.toStdString();
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
    _isDirectory = false;
}

bool ZipReader::isOpen() const
{
    return _isOpen && (_zipReader || _isDirectory);
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
        // If it's a directory, we can just return the list of files in that directory
        QDir dir(QString::fromStdString(_path));
        QFileInfoList fileInfoList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& fileInfo : fileInfoList)
        {
            fileList.push_back(fileInfo.fileName().toStdString());
        }
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
        if (_isDirectory)
        {
            QFile file(QString::fromStdString(_path + "/" + fileName));
            if (!file.open(QIODevice::ReadOnly))
            {
                return std::vector<uint8_t>();
            }
            QByteArray data = file.readAll();
            return std::vector<uint8_t>(data.begin(), data.end());
        }

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
        if (_isDirectory)
        {
            QFileInfo fileInfo(QString::fromStdString(_path + "/" + fileName));
            return fileInfo.exists() && fileInfo.isFile();
        }

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
