#include "fla_document.h"

FLADocument::FLADocument()
    : document(nullptr)
{}

FLADocument::~FLADocument()
{
    if (document)
    {
        delete document;
        document = nullptr;
    }
}
