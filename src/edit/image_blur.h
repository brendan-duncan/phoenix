#pragma once

#include <cstdint>

namespace fla {

/// Box-blurs an image of premultiplied 0xAARRGGBB pixels in place, the way
/// Flash's blur filter does: a running-sum box of about \a widthX by
/// \a widthY pixels, applied \a passes times (the filter's quality), which
/// with three passes is close to a Gaussian. A width under one pixel on an
/// axis leaves that axis alone.
void boxBlur(uint32_t* pixels, int width, int height, double widthX, double widthY, int passes);

} // namespace fla
