#pragma once

#include "../data/document.h"
#include "zip_reader.h"
#include <cstdint>
#include <string>

namespace fla {
class RawElement;
}

/// For a raw element that is a placed video (DOMVideoInstance): its
/// placement, frame and video, read from its XML, so it can be drawn.
void describeVideoInstance(fla::RawElement& raw);

/// The document's library videos, read from the media items it keeps as
/// read (extraMedia).
void collectVideos(fla::Document& document);

class DocumentParser
{
public:
    DocumentParser(ZipReader* zipReader);

    fla::Document* parse(const std::string& xmlContent, fla::DOMElement* parent);

    std::string errorString() const
    {
        return _errorString;
    }

private:
    ZipReader* _zipReader;
    std::string _errorString;
};
