#include "fla_parser.h"

#include "binary_fla.h"
#include "compound_file.h"
#include "document_parser.h"
#include "zip_reader.h"
#include <algorithm>
#include <fstream>
#include <iostream>

using namespace fla;

fla::FLADocument* FLAParser::parse(const std::string& filePath)
{
    // A .fla from Flash CS4 or earlier is not a zip at all but a Microsoft
    // compound document holding a binary format: it is converted to the
    // files of an XFL document, which are then read as any other.
    ZipReader zipReader;
    bool fromBinary = false;
    {
        std::ifstream file(filePath, std::ios::binary);
        unsigned char magic[8] = {};
        const unsigned char compound[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        if (file.read(reinterpret_cast<char*>(magic), sizeof(magic)) && std::equal(magic, magic + 8, compound))
        {
            file.seekg(0, std::ios::end);
            std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            CompoundFile compoundFile;
            if (!compoundFile.read(data))
            {
                _errorString = "This Flash CS4 (or earlier) document is damaged: " + compoundFile.errorString();
                return nullptr;
            }
            std::map<std::string, std::vector<uint8_t>> files;
            if (!BinaryFla::convert(compoundFile.streams(), files, _errorString))
                return nullptr;
            zipReader.openMemory(std::move(files));
            fromBinary = true;
        }
    }

    if (!zipReader.isOpen() && !zipReader.open(filePath))
    {
        // Handle error opening zip file
        _errorString = "Failed to open zip file: " + zipReader.errorString();
        return nullptr;
    }

    // An archive of a document folder rather than of the document: the
    // folder's DOMDocument.xml one level down.
    if (!zipReader.containsFile("DOMDocument.xml"))
    {
        for (const std::string& name : zipReader.getFileList())
        {
            const size_t slash = name.find('/');
            if (slash != std::string::npos && name.compare(slash + 1, std::string::npos, "DOMDocument.xml") == 0)
            {
                zipReader.setRoot(name.substr(0, slash + 1));
                break;
            }
        }
    }

    if (!zipReader.containsFile("DOMDocument.xml"))
    {
        // Handle missing XML file in zip
        _errorString = "DOMDocument.xml not found in zip file";
        return nullptr;
    }

    std::string documentFile = zipReader.readTextFile("DOMDocument.xml");
    if (documentFile.empty())
    {
        _errorString = "Failed to read XML file 'DOMDocument.xml' from zip";
        return nullptr;
    }

    FLADocument* flaDocument = new FLADocument(nullptr);
    flaDocument->filepath = filePath;
    flaDocument->fromBinary = fromBinary;

    DocumentParser parser(&zipReader);
    flaDocument->document = parser.parse(documentFile, flaDocument);
    if (flaDocument->document == nullptr)
    {
        _errorString = "Failed to parse XML content: " + parser.errorString();
        delete flaDocument;
        return nullptr;
    }

    return flaDocument;
}
