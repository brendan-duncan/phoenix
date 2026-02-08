#include "fla_parser.h"

#include "document_parser.h"
#include "zip_reader.h"
#include <iostream>

FLADocument* FLAParser::parse(const std::string& filePath)
{
    ZipReader zipReader;
    if (!zipReader.open(filePath))
    {
        // Handle error opening zip file
        _errorString = "Failed to open zip file: " + zipReader.errorString();
        return nullptr;
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

    FLADocument* flaDocument = new FLADocument();
    flaDocument->filepath = filePath;

    DocumentParser parser(&zipReader);
    flaDocument->document = parser.parse(documentFile);
    if (flaDocument->document == nullptr)
    {
        _errorString = "Failed to parse XML content: " + parser.errorString();
        delete flaDocument;
        return nullptr;
    }

    return flaDocument;
}
