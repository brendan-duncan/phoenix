#pragma once

#include "resource.h"
#include <string>
#include <vector>
#include <cstdint>

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

    std::vector<uint8_t> imageData; // Raw image data (e.g., JPEG or PNG bytes)

    Bitmap() = default;

    Type resourceType() const override { return Type::Bitmap; }

    std::string domTypeName() const override { return "Bitmap"; }
};
