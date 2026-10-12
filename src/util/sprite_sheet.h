#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sprites {

/// How frames are laid out on a sheet.
enum class Layout
{
    /// Every frame in a cell the size of the largest, in rows of \a
    /// columns.
    Grid,
    /// Frames side by side in one row.
    Strip,
    /// Frames fitted closely in rows no wider than the maximum width,
    /// tallest first.
    Packed
};

/// The description written beside the sheet.
enum class DataFormat
{
    /// A JSON object keyed by frame name, as many game engines read.
    JsonHash,
    /// The same with the frames in an array, in order.
    JsonArray,
    None
};

struct Options
{
    Layout layout = Layout::Grid;
    /// Grid columns; 0 for as square a sheet as fits.
    int columns = 0;
    /// The widest a packed sheet may be.
    int maxWidth = 2048;
    /// Clear pixels between frames, and around the sheet.
    int padding = 2;
    int border = 0;
    /// Each frame cut down to its drawn pixels, the offset kept.
    bool trim = false;
    /// Frames identical to an earlier one share its pixels.
    bool dedupe = true;
    /// The sheet's sides rounded up to powers of two.
    bool powerOfTwo = false;
};

/// One frame's pixels: red, green, blue and alpha bytes, row by row.
struct Frame
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

/// A frame's place on the sheet.
struct Placed
{
    /// Where on the sheet, and how big.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    /// Where in the frame the part kept starts; 0 unless trimmed.
    int offsetX = 0;
    int offsetY = 0;
    /// The frame's whole size.
    int sourceWidth = 0;
    int sourceHeight = 0;
    /// The earlier frame this one is a copy of, or -1.
    int sameAs = -1;
};

struct Sheet
{
    int width = 0;
    int height = 0;
    std::vector<Placed> frames;
};

/// The smallest rectangle holding \a frame's pixels that are not clear, as
/// x, y, width and height; all zero for a clear frame.
void trimBounds(const Frame& frame, int& x, int& y, int& width, int& height);

/// Where each frame goes.
Sheet layout(const std::vector<Frame>& frames, const Options& options);

/// The sheet's description: each frame's name, place, trim and size, the
/// image's file name and size, and the frame rate.
std::string describe(const Sheet& sheet, const std::vector<std::string>& names, const std::string& image,
    DataFormat format, double frameRate);

} // namespace sprites
