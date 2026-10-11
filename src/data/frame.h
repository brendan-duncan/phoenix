#pragma once

#include "dom_element.h"

#include <string>
#include <utility>
#include <vector>
#include "action_script.h"
#include "element.h"
#include "morph_shape.h"
#include "point.h"
#include <string>
#include <vector>

namespace fla {

class Frame : public DOMElement
{
public:
    int index = 0;
    int duration = 1;
    std::string keyMode;

    /// The frame label and its kind: "name" (the default when a name is
    /// set), "comment" or "anchor".
    std::string name;
    std::string labelType;
    TweenType tweenType = TweenType::None;
    bool motionTWeenSnap = false;
    bool motionTweenSync = false;

    /// A classic tween's easing: the Ease value, -100 (in) to 100 (out),
    /// or a custom ease curve when the frame has one (a chain of cubic
    /// beziers from (0,0) to (1,1): four points, then three per segment).
    int acceleration = 0;
    bool hasCustomEase = false;
    std::vector<Point> customEase;

    /// Rotation during a classic tween: "auto", "none", "clockwise" or
    /// "counter-clockwise", and how many extra whole turns.
    std::string motionTweenRotate;
    int motionTweenRotateTimes = 0;

    /// What the reader does not model, kept so it is written back as read:
    /// attributes such as a sound's (soundName, soundSync, inPoint44...) and
    /// child elements such as its SoundEnvelope, as XML text.
    std::vector<std::pair<std::string, std::string>> extraAttributes;

    /// A tween type Phoenix does not model ("motion object" for an object
    /// motion tween, "IK pose" for an armature's pose), kept as spelled and
    /// written back; tweenType is None for these.
    std::string otherTweenType;
    std::vector<std::string> extraChildren;

    ActionScript* actionScript = nullptr;
    MorphShape* morphShape = nullptr;
    std::vector<Element*> elements;

    Frame(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Frame() override;

    std::string domTypeName() const override { return "Frame"; }

    DOMType domType() const override { return DOMType::Frame; }

    static DOMType staticDomType() { return DOMType::Frame; }
};

} // namespace fla
