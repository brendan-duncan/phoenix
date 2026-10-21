#pragma once

#include "geom.h"
#include "graphics.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fla {
namespace display {

class BitmapData;

/// One of flash.filters' filters, with ActionScript's defaults: BlurFilter,
/// DropShadowFilter, GlowFilter, BevelFilter, GradientGlowFilter,
/// GradientBevelFilter, ColorMatrixFilter and ConvolutionFilter. Fields a
/// type does not use keep their defaults. A display object's filters are a
/// copy of what a script assigned, as Flash Player copies them.
struct BitmapFilter
{
    enum class Type
    {
        Blur,
        DropShadow,
        Glow,
        Bevel,
        GradientGlow,
        GradientBevel,
        ColorMatrix,
        Convolution,
        DisplacementMap
    };
    Type type = Type::Blur;

    double blurX = 4.0;
    double blurY = 4.0;
    /// Passes of the box blur, 1 (low) to 3 (high), up to 15.
    int quality = 1;

    /// Shadow and bevel: how far, and which way in degrees.
    double distance = 4.0;
    double angle = 45.0;
    /// Shadow and glow.
    uint32_t color = 0;
    double alpha = 1.0;
    double strength = 1.0;
    bool inner = false;
    bool knockout = false;
    bool hideObject = false;

    /// Bevel.
    uint32_t highlightColor = 0xffffff;
    double highlightAlpha = 1.0;
    uint32_t shadowColor = 0;
    double shadowAlpha = 1.0;
    /// "inner", "outer" or "full" (BitmapFilterType).
    std::string bevelType = "inner";

    /// Gradient glow and bevel.
    std::vector<Gradient::Stop> stops;

    /// ColorMatrixFilter: four rows of five, red, green, blue and alpha out
    /// of red, green, blue, alpha (0 to 255, unpremultiplied) and an offset.
    std::vector<double> matrix;

    /// ConvolutionFilter: the matrix (matrixX across by matrixY down), what
    /// its sum is divided by and added to, whether alpha is left alone, and
    /// what lies past the edge (the edge pixels, or color and alpha).
    int matrixX = 0;
    int matrixY = 0;
    double divisor = 1.0;
    double bias = 0.0;
    bool preserveAlpha = true;
    bool clamp = true;

    /// DisplacementMapFilter: each pixel taken from as far away as the
    /// map's componentX and componentY channels (1 red, 2 green, 4 blue, 8
    /// alpha; 128 none) say, times scaleX and scaleY in 256ths, the map
    /// placed at mapPoint; past the edge, by mode ("wrap", "clamp",
    /// "ignore", or "color" for color and alpha).
    std::shared_ptr<const BitmapData> mapBitmap;
    Point mapPoint;
    uint32_t componentX = 0;
    uint32_t componentY = 0;
    double scaleX = 0.0;
    double scaleY = 0.0;
    std::string mode = "wrap";

    static BitmapFilter blur(double blurX = 4.0, double blurY = 4.0, int quality = 1);
    static BitmapFilter dropShadow(double distance = 4.0, double angle = 45.0, uint32_t color = 0, double alpha = 1.0,
        double blurX = 4.0, double blurY = 4.0, double strength = 1.0, int quality = 1, bool inner = false,
        bool knockout = false, bool hideObject = false);
    static BitmapFilter glow(uint32_t color = 0xff0000, double alpha = 1.0, double blurX = 6.0, double blurY = 6.0,
        double strength = 2.0, int quality = 1, bool inner = false, bool knockout = false);
    static BitmapFilter bevel(double distance = 4.0, double angle = 45.0, uint32_t highlightColor = 0xffffff,
        double highlightAlpha = 1.0, uint32_t shadowColor = 0, double shadowAlpha = 1.0, double blurX = 4.0,
        double blurY = 4.0, double strength = 1.0, int quality = 1, const std::string& type = "inner",
        bool knockout = false);
    /// The identity unless \a matrix has twenty values.
    static BitmapFilter colorMatrix(const std::vector<double>& matrix = {});
    /// The color matrix Adjust Color (fl.motion.AdjustColor) makes of
    /// brightness, contrast and saturation (-100 to 100) and hue (-180 to
    /// 180 degrees).
    static BitmapFilter adjustColor(double brightness, double contrast, double saturation, double hue);
    static BitmapFilter convolution(int matrixX = 0, int matrixY = 0, const std::vector<double>& matrix = {},
        double divisor = 1.0, double bias = 0.0, bool preserveAlpha = true, bool clamp = true, uint32_t color = 0,
        double alpha = 0.0);

    static BitmapFilter displacementMap(std::shared_ptr<const BitmapData> mapBitmap, const Point& mapPoint,
        uint32_t componentX, uint32_t componentY, double scaleX, double scaleY, const std::string& mode, uint32_t color,
        double alpha);

    /// How far past the object's own pixels it can reach on each side, in
    /// the object's pixels: left, top, right, bottom.
    void reach(double& left, double& top, double& right, double& bottom) const;
};

/// \a rect grown by what \a filters reach past it.
Rect filteredRect(const Rect& rect, const std::vector<BitmapFilter>& filters);

/// Filters the core works itself, on premultiplied ARGB pixels (\a stride
/// in pixels): ColorMatrixFilter and ConvolutionFilter in place, and the
/// box blur BlurFilter is (\a blurX and \a blurY in pixels, \a passes
/// times).
void applyColorMatrix(uint32_t* pixels, int width, int height, int stride, const std::vector<double>& matrix);
void applyConvolution(uint32_t* pixels, int width, int height, int stride, const BitmapFilter& filter);
void applyBoxBlur(uint32_t* pixels, int width, int height, int stride, double blurX, double blurY, int passes);
/// DisplacementMapFilter in place, \a scale pixels to the object's.
void applyDisplacementMap(uint32_t* pixels, int width, int height, int stride, const BitmapFilter& filter,
    double scale = 1.0);

} // namespace display
} // namespace fla
