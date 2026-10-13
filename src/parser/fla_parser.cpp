#include "fla_parser.h"

#include "binary_fla.h"
#include "compound_file.h"
#include "document_parser.h"
#include "zip_reader.h"
#include "../data/bitmap.h"
#include "../data/document.h"
#include <algorithm>
#include <set>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

using namespace fla;

fla::FLADocument* FLAParser::parse(const std::string& filePath)
{
    // A damaged or hostile file must never take Phoenix down: anything
    // thrown while reading (a bad number, a size too big to allocate) ends
    // the read with an error instead.
    try
    {
        return parseUnguarded(filePath);
    }
    catch (const std::bad_alloc&)
    {
        _errorString = "The document is too large, or damaged: there is not enough memory to read it.";
    }
    catch (const std::exception& e)
    {
        _errorString = std::string("The document is damaged and could not be read (") + e.what() + ").";
    }
    catch (...)
    {
        _errorString = "The document is damaged and could not be read.";
    }
    return nullptr;
}

fla::FLADocument* FLAParser::parseUnguarded(const std::string& filePath)
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
            if (!BinaryFla::convert(compoundFile.streams(), files, _errorString, &_warningString))
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

    if (!fromBinary)
        keepUnreadFiles(zipReader, *flaDocument->document);
    return flaDocument;
}

void FLAParser::keepUnreadFiles(const ZipReader& zipReader, fla::Document& document)
{
    // The files of the package the document is not built from (publish
    // settings, mobile settings, metadata, the mimetype) are carried through
    // byte for byte, so a save writes them back.
    std::set<std::string> read = {"DOMDocument.xml", "bin/SymDepend.cache"};
    if (document.symbolList)
    {
        for (const fla::Symbol* symbol : document.symbolList->symbols)
        {
            if (symbol)
                read.insert("LIBRARY/" + symbol->href);
        }
    }
    for (const fla::Resource* resource : document.resources)
    {
        if (!resource || resource->resourceType() != fla::Resource::Type::Bitmap)
            continue;
        const fla::Bitmap* bitmap = static_cast<const fla::Bitmap*>(resource);
        read.insert("LIBRARY/" + bitmap->href);
        read.insert("bin/" + bitmap->bitmapDataHRef);
    }
    for (const auto& file : document.extraFiles)
        read.insert(file.first);

    for (const std::string& name : zipReader.getFileList())
    {
        if (name.empty() || name.back() == '/' || read.count(name))
            continue;
        // The project marker is written afresh, named after the document.
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".xfl") == 0 && name.find('/') == std::string::npos)
            continue;
        document.extraFiles.emplace_back(name, zipReader.lazyFile(name));
    }
}
