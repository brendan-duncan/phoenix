#pragma once

#include "../data/document.h"
#include "zip_reader.h"
#include <cstdint>
#include <string>

class DocumentParser
{
public:
    DocumentParser(ZipReader* zipReader);

    Document* parse(const std::string& xmlContent);

    std::string errorString() const
    {
        return _errorString;
    }

private:
    ZipReader* _zipReader;
    std::string _errorString;
};
