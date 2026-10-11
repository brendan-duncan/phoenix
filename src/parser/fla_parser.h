#pragma once

#include "../data/fla_document.h"

class ZipReader;

namespace fla {
class Document;
}

class FLAParser
{
public:
    fla::FLADocument* parse(const std::string& filePath);

    const std::string& errorString() const
    {
        return _errorString;
    }

private:
    static void keepUnreadFiles(const ZipReader& zipReader, fla::Document& document);

    std::string _errorString;
};
