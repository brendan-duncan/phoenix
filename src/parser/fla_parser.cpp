#include "fla_parser.h"

#include "document_parser.h"
#include "zip_reader.h"
#include <algorithm>
#include <fstream>
#include <iostream>

using namespace fla;

fla::FLADocument* FLAParser::parse(const std::string& filePath)
{
    // A .fla from Flash CS4 or earlier is not a zip at all but a Microsoft
    // compound document holding a binary format, which this reader does
    // not understand. Say so plainly rather than as a broken zip.
    {
        std::ifstream file(filePath, std::ios::binary);
        unsigned char magic[8] = {};
        if (file.read(reinterpret_cast<char*>(magic), sizeof(magic)))
        {
            const unsigned char compound[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
            if (std::equal(magic, magic + 8, compound))
            {
                _errorString = "This is a Flash CS4 (or earlier) document, saved in the old binary "
                    "format, which Phoenix cannot read. Open it in Animate and save it again to "
                    "convert it to the current format.";
                return nullptr;
            }
        }
    }

    ZipReader zipReader;
    if (!zipReader.open(filePath))
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
