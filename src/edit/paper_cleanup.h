#pragma once

#include <cstdint>
#include <vector>

namespace fla {

class Shape;

/// Paper drawings: a scanned or photographed pencil drawing cleaned up into
/// line art and traced into vector artwork. The paper's tint and uneven
/// light are taken out, the lines are told from the paper by how dark they
/// are, specks are removed, and what is left is traced. Free of Qt.
namespace paper {

struct Settings
{
    /// Take out uneven light and the paper's tint, so a page photographed
    /// under a lamp is as white in its corners as in the middle.
    bool evenPaper = true;
    /// Stretch the darkest lines to black and the paper to white.
    bool autoLevel = true;
    /// How dark a pixel must be to count as line, 0 to 255 once the paper
    /// is white: higher takes in fainter pencil.
    int threshold = 170;
    /// Dots of fewer pixels than this go, and holes of fewer pixels in a
    /// line are filled. 0 keeps everything.
    int despeckle = 6;
    /// Gaps in the lines up to this many pixels wide are closed. 0 leaves
    /// them.
    int closeGaps = 0;
    /// How much the lines' edges are smoothed before they are traced, 0 to 3.
    int smoothing = 1;
    /// The color the lines are drawn in.
    uint8_t lineColor[4] = {0, 0, 0, 255};
};

/// Each pixel's lightness, 0 to 255, from straight RGBA.
std::vector<uint8_t> lightness(const uint8_t* rgba, int width, int height);

/// The paper made even and white: each pixel against the paper around it,
/// then (with \a autoLevel) stretched so the paper is 255 and the darkest
/// lines 0.
std::vector<uint8_t> level(const std::vector<uint8_t>& gray, int width, int height, bool evenPaper, bool autoLevel);

/// A threshold that splits \a leveled into lines and paper, by where its
/// darker and lighter pixels divide best.
int suggestThreshold(const std::vector<uint8_t>& leveled);

/// Which pixels are line (1) and which paper (0).
std::vector<uint8_t> lineMask(const std::vector<uint8_t>& leveled, int width, int height, const Settings& settings);

/// The whole cleanup: the lines in the line color, the paper transparent,
/// as straight RGBA. \a linePixels, when given, is how many line pixels
/// there are.
std::vector<uint8_t> cleanup(const uint8_t* rgba, int width, int height, const Settings& settings,
    int* linePixels = nullptr);

/// The box round the line pixels of cleaned RGBA. False when there are none.
bool lineBounds(const std::vector<uint8_t>& cleaned, int width, int height, int& left, int& top, int& right,
    int& bottom);

/// Cleaned line art traced into a merge shape, a pixel to a unit, its
/// top-left at (0, 0), its curves fitted within \a curveFit pixels. Null
/// when there are no lines. The caller owns it.
Shape* vectorize(const std::vector<uint8_t>& cleaned, int width, int height, double curveFit);

} // namespace paper

} // namespace fla
