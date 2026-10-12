#pragma once

#include <string>
#include <vector>

/// A minimal OpenEXR writer: one part, scanlines, no compression, every
/// channel half float. Enough for compositing passes, which a compositor
/// (Nuke, After Effects, Fusion, Blender) reads as layers by channel name:
/// "line.R", "line.G", "line.B", "line.A", "matte.Y" and so on.
namespace exr {

struct Channel
{
    std::string name;
    /// width * height values, row by row from the top.
    std::vector<float> values;
};

/// Writes \a channels, each \a width by \a height, to \a path. False with
/// \a error when it cannot.
bool write(const std::string& path, int width, int height, std::vector<Channel> channels, std::string& error);

/// A float as the 16-bit half the file stores.
unsigned short toHalf(float value);

} // namespace exr
