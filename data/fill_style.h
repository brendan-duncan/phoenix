#pragma once

class FillStyle
{
public:
    enum class Type
    {
        SolidColor,
        LinearGradient,
        RadialGradient,
        BitmapFill
    };

    virtual Type type() const = 0;
};
