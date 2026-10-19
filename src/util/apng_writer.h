#pragma once

#include <cstdint>
#include <vector>

/// Writing animated PNGs, which Qt does not write and not every ffmpeg
/// does: full color with alpha, lossless, each frame after the first only
/// the part of it that changed. Free of Qt.
namespace apng {

class AnimationWriter
{
public:
    /// \a plays is how many times the animation plays; 0 for over and over.
    AnimationWriter(int width, int height, double frameRate, int plays);

    /// Adds a frame of four bytes a pixel, straight (not premultiplied)
    /// RGBA, top row first. A frame just like the one before lengthens it
    /// instead. False when the frame is not the animation's size.
    bool addFrame(const std::vector<uint8_t>& rgba);

    /// The file's bytes; empty when no frame was added.
    std::vector<uint8_t> finish();

private:
    struct Frame
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int delayNumerator = 1;
        std::vector<uint8_t> packed;
    };

    int _width;
    int _height;
    int _plays;
    int _delayNumerator;
    int _delayDenominator;
    std::vector<uint8_t> _previous;
    std::vector<Frame> _frames;
};

/// What an animated PNG says of itself, for checking one: false when the
/// bytes are not a PNG or a chunk's checksum is wrong.
struct Info
{
    int width = 0;
    int height = 0;
    /// The animation's frame count (acTL) and how many fcTL chunks there
    /// are, which should agree.
    int frames = 0;
    int frameControls = 0;
    int plays = 0;
};
bool inspect(const std::vector<uint8_t>& bytes, Info& info);

} // namespace apng
