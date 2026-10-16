#pragma once

#include "adjustment_effects.h"
#include "behaviors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class DOMElement;
class Layer;

/// Compositing on layers, Phoenix's own: a track matte (a layer shown
/// through the layer above it, by that layer's alpha or brightness) and
/// adjustment layers (effects on everything below them: blur, color, tint).
/// Kept in the layers' phoenixMatte, phoenixAdjustmentLayer and
/// phoenixAdjustments attributes, which other apps that open the FLA
/// ignore: they show the layers plainly. Free of Qt.
namespace compositing {

enum class Matte
{
    None,
    Alpha,
    AlphaInverted,
    Luma,
    LumaInverted
};

extern const char* const kMatte;
extern const char* const kAdjustmentLayer;
extern const char* const kAdjustments;

/// The matte \a layer is shown through, and its attributes set to \a matte.
Matte matteOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withMatte(const Layer& layer, Matte matte);
const char* matteName(Matte matte);

/// Whether \a layer is an adjustment layer, its effects, and its
/// attributes made one (or not).
bool isAdjustment(const Layer& layer);
std::vector<behaviors::Applied> effectsOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withAdjustment(const Layer& layer, bool on);

/// The same effects on one symbol instance, on its own picture rather than
/// on everything below: kept in its phoenixEffects attribute.
extern const char* const kInstanceEffects;
std::vector<behaviors::Applied> instanceEffectsOf(const DOMElement& element);
/// Whether \a element has an effect that is on.
bool hasInstanceEffects(const DOMElement& element);

/// How far past the picture \a list may draw on \a frame, in stage pixels:
/// the spread of a blur or a glow, an outline's width, a smear's length.
double reach(const std::vector<behaviors::Applied>& list, int frame);

/// A mask layer's edge: how far it is feathered and grown (shrunk, less
/// than 0), in pixels, and whether it shows what is outside it instead.
/// Kept in phoenixMaskFeather, phoenixMaskExpand and phoenixMaskInvert.
struct MaskEdge
{
    double feather = 0.0;
    double expand = 0.0;
    bool invert = false;
    bool plain() const { return feather <= 0.0 && expand == 0.0 && !invert; }
};
MaskEdge maskEdgeOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withMaskEdge(const Layer& layer, const MaskEdge& edge);

/// Variable feather: a shape, drawing object, symbol or text on a mask
/// layer can have a feather of its own, kept in its phoenixMaskFeather
/// attribute, in place of the layer's. One mask can then be soft round a
/// spotlight and hard along a window frame. Each keyframe's objects keep
/// their own, and a tween blends it from one keyframe's to the next's, so
/// an edge can soften or sharpen over time.
/// \a element's feather, or \a layerFeather when it has none.
double elementFeatherOf(const DOMElement& element, double layerFeather);
/// \a element's attributes with its feather set, or taken off (the layer's
/// used) when \a feather is less than 0.
std::vector<std::pair<std::string, std::string>> withElementFeather(const DOMElement& element, double feather);
/// The feather a tween shows \a t of the way from \a from to \a to (null
/// when the element does not tween), each falling back to \a layerFeather.
double blendedFeather(const DOMElement& from, const DOMElement* to, double t, double layerFeather);

/// Several masks: a masked layer is masked by its own mask layer (the one
/// it sits under) and by others of its timeline, named in its phoenixMasks
/// attribute. Add masks widen what shows, Intersect masks limit it to where
/// they cover too, and Subtract masks cut out where they cover, whatever
/// the order they are listed in: what shows is (own mask or any Add mask)
/// and every Intersect mask, less any Subtract mask.
enum class MaskCombine
{
    Add,
    Subtract,
    Intersect
};
struct MoreMask
{
    MaskCombine mode = MaskCombine::Add;
    /// The mask layer's name.
    std::string layer;
    bool operator==(const MoreMask& other) const { return mode == other.mode && layer == other.layer; }
};
extern const char* const kMoreMasks;
std::vector<MoreMask> moreMasksOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withMoreMasks(const Layer& layer, const std::vector<MoreMask>& masks);
const char* maskCombineName(MaskCombine mode);
/// \a own (one byte a pixel) with the coverage of each of \a others, all
/// the same size, combined by their modes as moreMasksOf describes.
void combineMasks(std::vector<uint8_t>& own, const std::vector<std::pair<MaskCombine, const std::vector<uint8_t>*>>& others);

/// How a layer works with what is drawn under it: Stencil shows what is
/// under only where the layer has art, Silhouette only where it has none,
/// Preserve Transparency draws the layer only where there is something
/// under it. Kept in phoenixLayerMode.
enum class LayerMode
{
    Normal,
    Stencil,
    Silhouette,
    Preserve
};
LayerMode layerModeOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withLayerMode(const Layer& layer, LayerMode mode);
const char* layerModeName(LayerMode mode);

/// \a coverage (one byte a pixel, \a width by \a height) with \a edge on
/// it, \a pixelsPerUnit pixels to a stage pixel: grown or shrunk, softened,
/// turned inside out.
void shapeMask(std::vector<uint8_t>& coverage, int width, int height, const MaskEdge& edge, double pixelsPerUnit);

/// The effects an adjustment layer can have, with their settings.
const std::vector<behaviors::Definition>& definitions();
const behaviors::Definition* find(const std::string& id);
behaviors::Applied defaults(const std::string& id, int seed);

/// \a list on an image (premultiplied ARGB, a row after another) on frame
/// \a frame of a movie at \a frameRate; \a pixelsPerUnit is how many of its
/// pixels a stage pixel covers, for the reach of a blur or a displacement.
/// \a coverage, when given (one byte a pixel), limits the effects to where
/// it is set, fading at its edges.
/// \a layers gives other layers' pictures, for Displacement Map.
/// With \a ownPicture the image is one instance's own picture: what
/// generators paint stays on its art rather than filling the image.
void apply(uint32_t* pixels, int width, int height, const std::vector<behaviors::Applied>& list, int frame,
    double pixelsPerUnit, const uint8_t* coverage = nullptr, double frameRate = 24.0,
    const effects::LayerPictures* layers = nullptr, bool ownPicture = false);

/// The matte's strength at each pixel of \a matte (premultiplied ARGB),
/// from 0 to 255, for \a kind.
std::vector<uint8_t> matteStrength(const uint32_t* matte, int width, int height, Matte kind);

} // namespace compositing

} // namespace fla
