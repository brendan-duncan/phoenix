#pragma once

#include <cstdint>
#include <string>

namespace fla {
namespace display {

/// flash.display.BlendMode: how a display object's pixels go over what is
/// under it.
enum class BlendMode
{
    Normal,
    Layer,
    Multiply,
    Screen,
    Lighten,
    Darken,
    Difference,
    Add,
    Subtract,
    Invert,
    Alpha,
    Erase,
    Overlay,
    Hardlight,
    Shader
};

/// The ActionScript string ("normal", "hardlight"...).
const char* blendModeName(BlendMode mode);
/// The mode a string names; false (and Normal) for none.
bool blendModeNamed(const std::string& name, BlendMode& mode);

/// \a source over \a backdrop in \a mode, both premultiplied ARGB.
uint32_t blendPixel(BlendMode mode, uint32_t source, uint32_t backdrop);

} // namespace display
} // namespace fla
