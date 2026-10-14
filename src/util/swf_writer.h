#pragma once

#include <cstdint>
#include <vector>

/// Writes SWF movies: shapes with solid, gradient and bitmap fills, and
/// lossless bitmaps, placed and removed frame by frame. Each shape has one
/// fill, filled even-odd, so a caller hands it outlines that do not cross
/// (simplified). Coordinates are in pixels; the file has them in twips.
/// Free of Qt.
namespace swf {

struct Color
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

/// x' = a x + c y + tx, y' = b x + d y + ty, in pixels.
struct Matrix
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
};

struct Fill
{
    enum class Type
    {
        Solid,
        Linear,
        Radial,
        Bitmap
    };
    Type type = Type::Solid;
    Color color;
    /// Gradients: offsets 0 to 1 and colors, at most 15; the placement of
    /// the gradient square (-819.2 to 819.2 pixels) in the shape; how it
    /// spreads (0 pad, 1 reflect, 2 repeat); a radial one's focal point,
    /// -1 to 1.
    struct Stop
    {
        double offset = 0.0;
        Color color;
    };
    std::vector<Stop> stops;
    Matrix matrix;
    int spread = 0;
    double focal = 0.0;
    /// Bitmaps: the bitmap's id, the placement of its pixels, whether it
    /// repeats past its edges and is smoothed.
    int bitmap = 0;
    bool repeat = false;
    bool smooth = true;
};

struct Segment
{
    enum class Kind
    {
        Move,
        Line,
        Quad
    };
    Kind kind = Kind::Line;
    /// The end, and for a quadratic curve its control.
    double x = 0.0, y = 0.0;
    double cx = 0.0, cy = 0.0;
};

class Writer
{
public:
    /// A movie of \a width by \a height pixels at \a fps on \a background.
    Writer(int width, int height, double fps, Color background);

    /// A bitmap of premultiplied ARGB pixels, row by row, four bytes each
    /// (alpha, red, green, blue); its character id.
    int defineBitmap(int width, int height, const std::vector<uint8_t>& argb);

    /// A shape filled with \a fill; its character id, or 0 when it has no
    /// outline.
    int defineShape(const Fill& fill, const std::vector<Segment>& outline);

    /// How a placed character mixes with what is under it, as SWF numbers
    /// them.
    enum class Blend
    {
        Normal = 1,
        Multiply = 3,
        Screen = 4,
        Lighten = 5,
        Darken = 6,
        Difference = 7,
        Add = 8,
        Subtract = 9,
        Overlay = 13,
        HardLight = 14
    };

    /// Puts character \a id at \a depth, its alpha scaled by \a alpha,
    /// blending as \a blend says.
    void place(int id, int depth, double alpha = 1.0, Blend blend = Blend::Normal);
    void remove(int depth);

    /// Starts a soundtrack streamed with the movie (SoundStreamHead2): its
    /// codec, rate and channels; frame blocks follow with soundBlock.
    void soundStream(int codec, int rate, bool stereo, int samplesPerFrame, int latency = 0);
    /// This frame's part of the soundtrack (SoundStreamBlock), before
    /// showFrame; an empty block writes nothing.
    void soundBlock(const std::vector<uint8_t>& data);

    void showFrame();

    /// The file: compressed (CWS) unless \a compress is false.
    std::vector<uint8_t> finish(bool compress = true);

    int frames() const { return _frames; }

private:
    void tag(int code, const std::vector<uint8_t>& body);

    int _width;
    int _height;
    double _fps;
    int _frames = 0;
    int _nextId = 1;
    std::vector<uint8_t> _tags;
};

} // namespace swf
