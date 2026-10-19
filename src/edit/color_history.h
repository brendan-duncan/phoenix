#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fla {

/// The colors drawn with lately, newest first, for the Color panel's row of
/// recent colors and the pop-up palette. A color used again moves to the
/// front rather than showing twice, and the oldest drop off past the limit.
/// Kept as text between runs. Free of Qt.
class ColorHistory
{
public:
    using Color = std::array<uint8_t, 4>;

    explicit ColorHistory(size_t limit = 16) : _limit(limit == 0 ? 1 : limit) {}

    /// Puts \a color first. True when the list changed.
    bool add(const Color& color);
    void clear() { _colors.clear(); }

    const std::vector<Color>& colors() const { return _colors; }
    size_t limit() const { return _limit; }
    /// Keeps the newest \a limit.
    void setLimit(size_t limit);

    /// "#RRGGBBAA" each, newest first, parted by spaces.
    std::string toText() const;
    /// The colors \a text lists, as toText() writes them ("#RRGGBB" is
    /// opaque); anything else in it is passed over.
    static ColorHistory fromText(const std::string& text, size_t limit = 16);

private:
    std::vector<Color> _colors;
    size_t _limit;
};

/// Hue, saturation and value: hue in degrees, 0 up to 360, the others 0 to 1.
struct Hsv
{
    double h = 0.0;
    double s = 0.0;
    double v = 0.0;
};

/// \a red, \a green, \a blue from 0 to 255 as hue, saturation and value. A
/// gray has hue 0.
Hsv hsvOf(uint8_t red, uint8_t green, uint8_t blue);
/// The color \a hsv names, its alpha \a alpha. The hue wraps; the others
/// are kept within 0 to 1.
std::array<uint8_t, 4> rgbOf(const Hsv& hsv, uint8_t alpha = 255);

} // namespace fla
