#include "phoenix_features.h"

#include "../data/document.h"
#include "../data/fill_style.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <tinyxml2.h>

#include <functional>
#include <map>

namespace fla {
namespace features {

const std::vector<Feature>& all()
{
    static const std::vector<Feature> list = {
        {"textured-brushes", "Textured brushes", "the grain baked into a bitmap fill, which can no longer be changed",
            {"phoenixTexture", "phoenixTextureScale", "phoenixTextureStrength", "phoenixTextureOffset",
                "phoenixTextureRotation", "phoenixTextureFixed"}},
        {"art-and-pattern-brushes", "Art and pattern brushes", "plain strokes along the same lines",
            {"phoenixBrush", "phoenixBrushPattern"}},
        {"bitmap-painting", "Bitmap painting", "the painted canvas as an ordinary bitmap", {"phoenixPaint"}},
        {"linked-palettes", "Linked palettes", "the colors as they are, without the palettes or their links",
            {"phoenixPalettes", "phoenixColor"}},
        {"effect-layers", "Effect layers", "hidden guide layers: the tone, highlight or glow is missing",
            {"phoenixEffect"}},
        {"line-and-color-art", "Line and color art layers", "ordinary layers", {"phoenixLineArt"}},
        {"review-notes", "Review notes", "the notes as text on a guide layer, which is not exported",
            {"phoenixReview", "phoenixNote", "phoenixNoteAuthor", "phoenixNoteDone"}},
        {"multiplane-camera", "Multiplane camera", "a guide layer: the shot is not framed, moved, colored or given depth",
            {"phoenixCamera", "phoenixCameraX", "phoenixCameraY", "phoenixCameraZoom", "phoenixCameraRotation",
                "phoenixDepth", "phoenixCameraBrightness", "phoenixCameraTintMultiplier", "phoenixCameraTintColor",
                "phoenixCameraRedMultiplier", "phoenixCameraGreenMultiplier", "phoenixCameraBlueMultiplier",
                "phoenixCameraAlphaMultiplier", "phoenixCameraRedOffset", "phoenixCameraGreenOffset",
                "phoenixCameraBlueOffset", "phoenixCameraAlphaOffset"}},
        {"pegs", "Pegs", "ordinary symbols", {"phoenixPeg"}},
        {"deformers", "Deformers", "the instances without their bend", {"phoenixDeformer"}},
        {"behaviors", "Behaviors", "the instances as keyed, without the motion their behaviors add",
            {"phoenixBehaviors"}},
        {"shape-operators", "Shape operators", "the shapes as drawn, without Trim Paths, Wiggle Path or Zig-zag",
            {"phoenixShapeOperators"}},
        {"text-animators", "Text animators", "the text as typed, its letters still", {"phoenixTextAnimators"}},
        {"compositing", "Track mattes and adjustment layers",
            "matte layers drawn plainly and adjustment layers without their blur or color",
            {"phoenixMatte", "phoenixAdjustmentLayer", "phoenixAdjustments"}},
        {"instance-effects", "Effects on instances", "the instances without their blurs, glows, distortions and wipes",
            {"phoenixEffects"}},
        {"node-graph", "Node graphs", "the layers stacked as usual, without the graph's effects, merges and mattes",
            {"phoenixNodeGraph"}},
        {"mask-edges", "Mask edges and layer modes", "masks hard-edged and not inverted, layers drawn normally", {"phoenixMaskFeather", "phoenixMaskExpand", "phoenixMaskInvert", "phoenixLayerMode"}},
        {"several-masks", "Several masks on a layer", "each masked layer masked by its own mask layer only",
            {"phoenixMasks"}},
        {"motion-blur", "Motion blur, echo and posterize time", "the layers drawn sharp, every frame, without echoes", {"phoenixMotionBlur", "phoenixShutterAngle", "phoenixMotionBlurSamples", "phoenixEcho", "phoenixPosterize"}},
        {"templates", "Template fields", "the symbols' text and colors as drawn, the same in every instance, nothing pinned to the text",
            {"phoenixTemplate", "phoenixPin"}},
        {"master-controllers", "Master controllers", "the properties as keyed, without the sliders",
            {"phoenixControllers"}},
        {"3d-rotation", "3D rotation and depth", "the movie clips flat, without their 3D turn, lighting, shadows or depth",
            {"phoenixRotationX", "phoenixRotationY", "phoenixZ", "phoenixPerspectiveAngle", "phoenixLight", "phoenixDepthOfField",
                "phoenixShadows", "phoenixExtrude"}},
        {"depth-sorting", "Sorting by depth", "each layer's clips stacked in the order they were placed, whatever their Z",
            {"phoenixSortByDepth"}},
        {"parenting-options", "Layer parenting options", "child layers taking on all of the parent's transform",
            {"phoenixParentScale", "phoenixParentSkew", "phoenixParentFlip"}},
        {"bone-corrections", "Bone corrections",
            "the boned shapes as bent, without their corrections, and the drawings and fades the bones drive as keyed",
            {"phoenixCorrections"}},
        {"shape-binding", "Bones bound in Phoenix", "boned shapes as drawn; posing them may bend them differently",
            {"phoenixBinding", "phoenixSmoothness"}},
        {"bone-forces", "Gravity and wind on bones", "the bones' sway as baked into their frames, no longer worked out again",
            {"phoenixGravity", "phoenixGravityAngle", "phoenixWind", "phoenixWindAngle", "phoenixWindGusts",
                "phoenixWindRate", "phoenixWindSeed"}},
        {"bone-controls", "Bone locks, targets and stretch",
            "the poses as made, which play the same, but the bones no longer lock, reach, keep their angles or stretch when posed",
            {"phoenixLocked", "phoenixTarget", "phoenixTargetPoint", "phoenixIndependent", "phoenixSquash", "phoenixStretch",
                "phoenixLength"}},
        {"shape-hints", "Shape hints", "shape tweens without the hints steering them", {"phoenixShapeHints"}},
        {"mouth-charts", "Lip sync mouth charts", "the mouth frames as set, without the chart",
            {"phoenixMouthChart"}},
        {"layer-transparency", "Layer transparency", "the layers drawn solid", {"phoenixTransparency"}},
        {"see-through-stage", "See-through stage", "an opaque stage color", {"phoenixStageAlpha"}},
        {"safe-zones", "Safe zones and aspect-ratio guides", "the stage without them", {"phoenixSafeZones"}},
        {"reference-board", "Reference images", "the document without its pinned reference images",
            {"phoenixReferences"}},
    };
    return list;
}

namespace {

const Feature& otherSettings()
{
    static const Feature other{"other-phoenix-settings", "Other Phoenix settings", "the document without them", {}};
    return other;
}

/// The feature an attribute marks: one of all(), the catch-all for another
/// phoenix... attribute, or null for any other attribute.
const Feature* featureOf(const std::string& attribute)
{
    static const std::map<std::string, const Feature*> byAttribute = []() {
        std::map<std::string, const Feature*> map;
        for (const Feature& feature : all())
        {
            for (const char* name : feature.attributes)
                map[name] = &feature;
        }
        return map;
    }();
    const auto found = byAttribute.find(attribute);
    if (found != byAttribute.end())
        return found->second;
    // Workspace and theme choices stored with a document are the person's
    // set-up, not the document's content.
    if (attribute == "phoenixWorkspace" || attribute == "phoenixTheme")
        return nullptr;
    return attribute.compare(0, 7, "phoenix") == 0 ? &otherSettings() : nullptr;
}

using Attributes = std::vector<std::pair<std::string, std::string>>;

} // namespace

std::vector<Use> used(const Document& document)
{
    std::map<const Feature*, Use> found;
    const auto note = [&](const Attributes& attributes, const std::string& where) {
        for (const auto& attribute : attributes)
        {
            const Feature* feature = featureOf(attribute.first);
            if (!feature)
                continue;
            Use& use = found[feature];
            if (use.count++ == 0)
            {
                use.feature = feature;
                use.where = where;
            }
        }
    };

    const auto fill = [&](const FillStyle* style, const std::string& where) {
        if (style)
            note(style->extraAttributes, where);
    };
    std::function<void(const Element*, const std::string&)> element = [&](const Element* item, const std::string& where) {
        if (!item)
            return;
        note(item->extraAttributes, where);
        switch (item->elementType())
        {
        case Element::Type::Shape:
        {
            const Shape* shape = static_cast<const Shape*>(item);
            for (const FillStyle* style : shape->fills)
                fill(style, where);
            for (const StrokeStyle* stroke : shape->strokes)
            {
                if (!stroke)
                    continue;
                note(stroke->extraAttributes, where);
                fill(stroke->fill, where);
            }
            break;
        }
        case Element::Type::StaticText:
            for (const TextRun& run : static_cast<const StaticText*>(item)->runs)
                note(run.extraAttributes, where);
            break;
        case Element::Type::Group:
            for (const Element* member : static_cast<const Group*>(item)->members)
                element(member, where);
            break;
        default:
            break;
        }
    };
    const auto timeline = [&](const Timeline* each, const std::string& owner) {
        if (!each)
            return;
        note(each->extraAttributes, owner);
        for (const Layer* layer : each->layers)
        {
            if (!layer)
                continue;
            const std::string where = owner + " > " + layer->name;
            note(layer->extraAttributes, where);
            for (const Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                const std::string at = where + ", frame " + std::to_string(frame->index + 1);
                note(frame->extraAttributes, at);
                // An armature's own, on the IKTree its pose frame keeps.
                for (const std::string& child : frame->extraChildren)
                {
                    if (child.compare(0, 7, "<IKTree") != 0 || child.find("phoenix") == std::string::npos)
                        continue;
                    tinyxml2::XMLDocument tree;
                    const tinyxml2::XMLElement* root =
                        tree.Parse(child.c_str()) == tinyxml2::XML_SUCCESS ? tree.FirstChildElement("IKTree") : nullptr;
                    // The tree's own, and its nodes' and their poses'.
                    Attributes attributes;
                    std::function<void(const tinyxml2::XMLElement*)> gather = [&](const tinyxml2::XMLElement* element) {
                        for (const tinyxml2::XMLAttribute* each = element->FirstAttribute(); each; each = each->Next())
                            attributes.emplace_back(each->Name(), each->Value());
                        for (const tinyxml2::XMLElement* below = element->FirstChildElement(); below; below = below->NextSiblingElement())
                            gather(below);
                    };
                    if (root)
                        gather(root);
                    note(attributes, at);
                }
                for (const Element* item : frame->elements)
                    element(item, at);
            }
        }
    };

    note(document.extraAttributes, "the document");
    // The stage's transparency is read into its color (phoenixStageAlpha on
    // the way in and out), so it is seen there.
    if (document.backgroundColor[3] != 255)
        note({{"phoenixStageAlpha", std::string()}}, "the document");
    for (const Timeline* scene : document.timelines)
        timeline(scene, scene ? scene->name : std::string());
    if (document.symbolList)
    {
        for (const Symbol* symbol : document.symbolList->symbols)
        {
            if (!symbol)
                continue;
            note(symbol->extraAttributes, symbol->name);
            for (const Timeline* each : symbol->timelines)
                timeline(each, symbol->name);
        }
    }

    std::vector<Use> uses;
    for (const Feature& feature : all())
    {
        const auto use = found.find(&feature);
        if (use != found.end())
            uses.push_back(use->second);
    }
    const auto other = found.find(&otherSettings());
    if (other != found.end())
        uses.push_back(other->second);
    return uses;
}

std::vector<std::string> ids(const std::vector<Use>& uses)
{
    std::vector<std::string> list;
    for (const Use& use : uses)
        list.push_back(use.feature->id);
    return list;
}

} // namespace features
} // namespace fla
