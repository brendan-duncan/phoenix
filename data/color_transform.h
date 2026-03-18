#pragma once

#include <cstdint>

namespace fla {

class ColorTransform
{
public:
    double tintMultiplier = 0.0;
    uint8_t tintColor[4] = {0, 0, 0, 255}; // RGBA
    double brightness = 0.0;

    double redMultiplier = 1.0;
    double greenMultiplier = 1.0;
    double blueMultiplier = 1.0;
    double alphaMultiplier = 1.0;
    int redOffset = 0;
    int greenOffset = 0;
    int blueOffset = 0;
    int alphaOffset = 0;

    ColorTransform() = default;

    bool isIdentity() const
    {
        return tintMultiplier == 0.0 &&
               brightness == 0.0 &&
               redMultiplier == 1.0 &&
               greenMultiplier == 1.0 &&
               blueMultiplier == 1.0 &&
               alphaMultiplier == 1.0 &&
               redOffset == 0 &&
               greenOffset == 0 &&
               blueOffset == 0 &&
               alphaOffset == 0;
    }
};

} // namespace fla
