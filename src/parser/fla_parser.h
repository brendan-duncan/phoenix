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
    /// Reads the SWF movie \a swf as a document named \a filePath (a movie
    /// loaded by another as it plays); null, with errorString(), when it
    /// cannot. Never throws.
    fla::FLADocument* parseSwf(const std::vector<uint8_t>& swf, const std::string& filePath);

    const std::string& errorString() const
    {
        return _errorString;
    }

    /// What of a document that did open could not be read, for the person
    /// opening it; empty when all of it was.
    const std::string& warningString() const
    {
        return _warningString;
    }

private:
    template <typename Read>
    fla::FLADocument* guarded(Read read);
    fla::FLADocument* parseUnguarded(const std::string& filePath, const std::vector<uint8_t>* swf = nullptr);
    static void keepUnreadFiles(const ZipReader& zipReader, fla::Document& document);

    std::string _errorString;
    std::string _warningString;
};
