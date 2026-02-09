#pragma once

class Resource
{
public:
    enum class Type
    {
        Bitmap,
        Font
    };

    Resource() = default;

    virtual Type resourceType() const = 0;
};
