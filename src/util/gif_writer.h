#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Writing GIF images, which Qt reads but does not write: the colors cut
/// down to a palette of at most 256 (median cut, weighted by how much of
/// the picture each color covers), the pixels LZW-compressed. Free of Qt.
namespace gif {

/// How a picture is cut down to a palette.
struct Options
{
    /// Colors at most, 2 to 256.
    int colors = 256;
    /// Error diffusion (Floyd and Steinberg): what each pixel misses of its
    /// color is spread onto its neighbors, so gradients keep their look in
    /// few colors.
    bool dither = false;
    /// For a looping animation, how many times it plays after the first;
    /// 0 for over and over.
    int repeat = 0;
};

/// A picture cut down to a palette: its colors (red, green, blue), an index
/// a pixel, and the index kept for transparent pixels (-1 for none).
struct Reduced
{
    std::vector<uint8_t> palette;
    std::vector<uint8_t> indices;
    int clearIndex = -1;
};

/// \a rgba (\a width by \a height, four bytes a pixel) cut down as \a
/// options say, for a GIF or an 8-bit PNG.
Reduced reduce(int width, int height, const std::vector<uint8_t>& rgba, bool transparent, const Options& options);

/// An image of \a width by \a height pixels, \a rgba holding four bytes a
/// pixel (not premultiplied). Pixels whose alpha is under half become the
/// transparent color when \a transparent; otherwise alpha is ignored.
/// Returns the GIF file's bytes, empty when the size is out of range.
std::vector<uint8_t> encode(int width, int height, const std::vector<uint8_t>& rgba, bool transparent,
    const Options& options = Options());

/// An animated GIF, a frame at a time: each frame with a palette of its own
/// (so colors stay true through a long animation), shown for its delay, and
/// the whole looping forever when asked.
class AnimationWriter
{
public:
    AnimationWriter(int width, int height, bool loop, const Options& options = Options());

    /// Adds a frame of four bytes a pixel, shown for \a delay hundredths of
    /// a second. False when the frame is not the animation's size.
    bool addFrame(const std::vector<uint8_t>& rgba, int delay, bool transparent);

    /// The file's bytes, the animation ended.
    std::vector<uint8_t> finish();

    /// The delay of frame \a index at \a frameRate, in hundredths of a
    /// second, so the frames' times add up to the movie's (GIF counts in
    /// whole hundredths).
    static int delayOf(int index, double frameRate);

private:
    int _width;
    int _height;
    Options _options;
    std::vector<uint8_t> _out;
};

} // namespace gif
