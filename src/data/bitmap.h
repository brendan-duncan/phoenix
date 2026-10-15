#pragma once

#include "lazy_bytes.h"
#include "resource.h"
#include <string>
#include <vector>
#include <cstdint>

namespace fla {

class Bitmap : public Resource
{
public:
    std::string name;
    std::string itemId;
    std::string sourceExternalFilepath;
    uint32_t sourceLastImported = 0;
    uint32_t externalFileCRC32 = 0;
    uint32_t externalFileSize = 0;
    bool allowSmoothing = false;
    int quality = 0;
    std::string href;
    std::string bitmapDataHRef;
    int frameRight = 0;
    int frameBottom = 0;
    bool isJPEG = false;

    /// The imported file as the library keeps it (a PNG, a JPEG), when
    /// there is one; read from a Phoenix document only when first needed.
    LazyBytes imageData;

    /// The bin/<bitmapDataHRef> entry as stored: the FLA's own encoding of
    /// the pixels (see bitmap_decoder.h), kept so a save carries it through
    /// and so files whose LIBRARY folder holds no copy of the image still
    /// have something to draw.
    LazyBytes datData;

    Bitmap(DOMElement* parent)
        : Resource(parent)
    {}

    Type resourceType() const override { return Type::Bitmap; }

    std::string domTypeName() const override { return "Bitmap"; }

    DOMType domType() const override { return DOMType::Bitmap; }

    static DOMType staticDomType() { return DOMType::Bitmap; }
};

} // namespace fla
