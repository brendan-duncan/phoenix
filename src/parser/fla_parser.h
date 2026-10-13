#pragma once

#include "../data/fla_document.h"

class ZipReader;

namespace fla {
class Document;
}

class FLAParser
{
public:
    /// Reads the document at \a filePath; null, with errorString(), when it
    /// cannot. Never throws.
    fla::FLADocument* parse(const std::string& filePath);

    const std::string& errorString() const
    {
        return _errorString;
    }

private:
    fla::FLADocument* parseUnguarded(const std::string& filePath);
    static void keepUnreadFiles(const ZipReader& zipReader, fla::Document& document);

    std::string _errorString;
};
