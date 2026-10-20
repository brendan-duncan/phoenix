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

    /// One straight (not premultiplied) color, 0 to 255 a channel, as the
    /// transform makes it: tint, then brightness, then each channel
    /// multiplied and offset, clamped.
    void apply(int& r, int& g, int& b, int& a) const
    {
        if (tintMultiplier != 0.0)
        {
            // Tint multiplies each color channel by 1 - t and adds t of the
            // tint color; alpha is left alone.
            const double t = tintMultiplier;
            const double it = 1.0 - t;
            r = static_cast<int>(it * r + t * tintColor[0]);
            g = static_cast<int>(it * g + t * tintColor[1]);
            b = static_cast<int>(it * b + t * tintColor[2]);
        }
        if (brightness != 0.0)
        {
            // Brightness is a multiply and add too: towards white it
            // multiplies by 1 - b and adds 255 b, towards black it
            // multiplies by 1 + b.
            const double br = brightness;
            if (br > 0.0)
            {
                r = static_cast<int>(r * (1.0 - br) + 255.0 * br);
                g = static_cast<int>(g * (1.0 - br) + 255.0 * br);
                b = static_cast<int>(b * (1.0 - br) + 255.0 * br);
            }
            else
            {
                r = static_cast<int>(r * (1.0 + br));
                g = static_cast<int>(g * (1.0 + br));
                b = static_cast<int>(b * (1.0 + br));
            }
        }
        r = static_cast<int>(r * redMultiplier) + redOffset;
        g = static_cast<int>(g * greenMultiplier) + greenOffset;
        b = static_cast<int>(b * blueMultiplier) + blueOffset;
        a = static_cast<int>(a * alphaMultiplier) + alphaOffset;
        const auto clamp = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
        r = clamp(r);
        g = clamp(g);
        b = clamp(b);
        a = clamp(a);
    }
};

} // namespace fla
