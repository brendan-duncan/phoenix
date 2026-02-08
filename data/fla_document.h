#pragma once

#include "document.h"

class FLADocument
{
public:
    std::string filepath;
    Document* document;

    FLADocument();

    ~FLADocument();
};
