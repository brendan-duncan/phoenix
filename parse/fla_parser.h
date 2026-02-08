#pragma once

#include "../data/fla_document.h"

class FLAParser
{
public:
    FLADocument* parse(const std::string& filePath);

    const std::string& errorString() const
    {
        return _errorString;
    }

private:
    std::string _errorString;
};
