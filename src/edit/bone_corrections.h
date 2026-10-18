#pragma once

#include "ik_pose.h"

#include "../data/point.h"
#include "../data/transform.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;
class Frame;
class Shape;
class SymbolInstance;
class Timeline;

/// Bone corrections: a bone's turn driving the art it does not bend by
/// itself. As a bone turns from one angle to another, a boned shape's
/// points move part way to a corrected pose drawn at the far angle (an elbow
/// that keeps its crease, a bicep that bulges), a graphic swaps its drawing
/// across a run of frames (a hand turning, a head turning) or an instance
/// fades. The bone is named by the joint it turns about, as the Joint
/// Rotation dialog sets that joint; its angle is the joint's turn from the
/// bone above it, or, at the top of the armature, its own.
///
/// They are worked out on every frame from the armature's pose, kept on the
/// instance they change in its phoenixCorrections attribute (a boned
/// shape's on its ik container instance), which other apps that open the
/// FLA ignore; bake() makes them what any player shows. Free of Qt.
namespace corrections {

/// The attribute corrections are kept in.
extern const char* const kAttribute;

/// How far one point of a boned shape moves when its correction is in
/// full, in the armature's space as its rest pose (the span's first frame)
/// has the point's bones. Points are numbered as points() lists them, in
/// the shape that is element \a element of the frame shown on layer
/// \a layer of the ik container.
struct PointShift
{
    int layer = 0;
    int element = 0;
    int point = 0;
    double dx = 0.0;
    double dy = 0.0;
};

/// What the corrections can change.
enum class Property
{
    /// A boned shape's points (the instance is its ik container).
    Shape,
    /// The frame of its symbol a graphic shows, from 1.
    Frame,
    /// The instance's alpha, in percent.
    Alpha
};

struct Correction
{
    /// The armature's name (Armature_1) and the joint's node (ikNode_2).
    std::string armature;
    std::string joint;
    Property property = Property::Shape;
    /// The bone's angles, in degrees, where the correction starts and where
    /// it is in full; between them it blends in a straight line.
    double from = 0.0;
    double to = 90.0;
    /// A frame's or an alpha's value at each end.
    double valueFrom = 0.0;
    double valueTo = 0.0;
    /// A shape correction's points.
    std::vector<PointShift> shifts;

    bool operator==(const Correction& other) const;
};

/// The corrections on \a instance, in the order they apply.
std::vector<Correction> of(const SymbolInstance& instance);
bool any(const SymbolInstance& instance);
/// \a instance's attributes with \a list kept in them (none when empty).
std::vector<std::pair<std::string, std::string>> withCorrections(const SymbolInstance& instance,
    const std::vector<Correction>& list);
std::string encode(const std::vector<Correction>& list);
std::vector<Correction> decode(const std::string& text);

/// Whether any instance in \a document has corrections.
bool anyIn(const Document& document);

/// How far in a correction is when its bone is at \a degrees: 0 at from,
/// 1 at to, held past either end.
double weight(const Correction& correction, double degrees);

/// The frame (from 0) and the alpha factor an instance's frame and alpha
/// corrections give it: the last frame correction wins, alphas multiply.
/// -1 for no frame.
struct Effect
{
    int frame = -1;
    double alpha = 1.0;
};

/// A joint's turn, in degrees from -180 to 180: from its parent joint's
/// angle, or its own at the top of the armature.
double jointAngle(const std::vector<IKJoint>& joints, int joint);

/// The joint whose node is named \a node; -1 when there is none.
int jointNamed(const IKArmature& armature, const std::string& node);

/// The IKTree a pose frame keeps; null when it keeps none.
const std::string* treeOf(const Frame& frame);

/// Every point of every segment of \a shape, curves' control points too,
/// edge by edge: what a shape correction numbers. Bending a shape keeps its
/// edges, so every frame of a boned shape numbers its points the same.
std::vector<Point> points(const Shape& shape);

/// Armatures found and read once, to work corrections out frame after
/// frame. Forget everything with clear() when the document changes.
class Rig
{
public:
    Rig();
    ~Rig();
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void clear();

    /// The armature kept on \a pose, read once; null when it keeps none.
    const IKArmature* armatureOf(const Frame& pose);

    /// The armature named \a name on \a timeline as it is at \a frame: its
    /// pose span there, or else the nearest one, and how far into it
    /// \a frame is (held at the span's ends). Null when there is none.
    const IKArmature* armatureAt(const Timeline& timeline, const std::string& name, int frame, int& into);

    /// The angle of \a correction's bone on \a frame of \a timeline; false
    /// when its armature or joint is not there.
    bool angleAt(const Timeline& timeline, int frame, const Correction& correction, double& degrees);

    /// What \a instance's frame and alpha corrections make of it on \a frame
    /// of the timeline it is on.
    Effect effect(const SymbolInstance& instance, int frame);
    /// The same for corrections \a list of an instance on \a timeline.
    Effect effect(const Timeline& timeline, const std::vector<Correction>& list, int frame);

private:
    struct Parsed
    {
        std::string xml;
        std::shared_ptr<IKArmature> armature;
    };
    std::map<const Frame*, Parsed> _parsed;
};

/// The joints \a armature shows \a into frames into its span, springs and
/// all.
std::vector<IKJoint> jointsAt(const IKArmature& armature, int into);

/// A boned shape's points moved by its corrections: \a shape is what its ik
/// container \a container shows \a into frames into the span (element
/// \a element of the frame on layer \a layer), \a rest the same shape on
/// the span's first frame, and \a armature the one bending it. Each shape
/// correction in \a list moves the points by its shifts at its weight,
/// turned with the bones that carry each point. A correction on the joint
/// \a leaveOut is passed over. False when nothing moved.
bool correctShape(Shape& shape, const Shape& rest, const SymbolInstance& container, const IKArmature& armature,
    int into, int layer, int element, const std::vector<Correction>& list, const std::string& leaveOut = std::string());

/// The shifts that make \a base into \a edited, both what the container
/// shows \a into frames into the span (element \a element of layer
/// \a layer), turned back to the rest pose's bones, for a correction in
/// full there. False when \a edited's points are not \a base's (points were
/// added or taken away).
bool recordShifts(const Shape& base, const Shape& edited, const Shape& rest, const SymbolInstance& container,
    const IKArmature& armature, int into, int layer, int element, std::vector<PointShift>& shifts);

/// The ik container instance on a boned shape's pose frame; null for an
/// armature of symbols.
SymbolInstance* containerOn(const Frame& pose);

/// The joints of the armature named \a armature that drive corrections on
/// \a timeline, by node name.
std::set<std::string> drivingJoints(const Timeline& timeline, const std::string& armature);

/// Corrections made what any player shows, for a copy of \a document being
/// written for other apps or published: a boned shape's frames are each
/// corrected as they show; an instance with frame or alpha corrections
/// shows a new graphic symbol of its own instead, holding a keyframe of its
/// symbol at the frame and alpha each frame calls for. The attributes go.
/// Returns how many instances were baked; \a notes says what was done.
int bake(Document& document, std::vector<std::string>& notes);

} // namespace corrections

} // namespace fla
