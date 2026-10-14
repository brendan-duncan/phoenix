#pragma once

#include "command.h"

#include "../data/point.h"
#include "../data/transform.h"

#include <memory>
#include <string>
#include <vector>

namespace tinyxml2 {
class XMLDocument;
class XMLElement;
}

namespace fla {

class Frame;
class Layer;
class Shape;
class Symbol;
class SymbolInstance;
class Timeline;

/// One joint of an armature at one frame, in the coordinates of the timeline
/// the armature is on.
///
/// Each joint carries a frame of its own: its place and an angle. The joints
/// below it hang off that frame at fixed offsets, so turning a joint swings
/// everything below it round its place. A bone runs from a joint to each of
/// its children and moves with its parent joint's frame.
struct IKJoint
{
    Point position;
    double angle = 0.0;
    /// The joint above, by index; -1 for the joint at the armature's root.
    int parent = -1;
};

/// How far a joint may turn from its parent bone, in radians, when it is
/// constrained. Kept on the joint's node for Phoenix (phoenixMinRotation,
/// phoenixMaxRotation); the files Animate writes carry no limits there.
struct JointLimit
{
    bool constrained = false;
    double min = 0.0;
    double max = 0.0;
};

/// One joint of an armature of symbol instances, as the Bone tool builds it.
struct ArmatureJoint
{
    /// Where the joint is: the instance's transformation point.
    Point position;
    int parent = -1;
    /// The node's name ("ikNode_3"), which its instance takes as its name.
    std::string node;
    /// The referenceID of the instance the joint moves.
    std::string reference;
    /// The instance's registration point (its tx, ty).
    Point registration;
    JointLimit limit;
};

/// One bone a point of a boned shape follows, and how much. A bone is
/// numbered by the joint at its tail, as IKArmature numbers joints.
struct BoneWeight
{
    int bone = -1;
    double weight = 1.0;
};

/// The bones each control point of a boned shape follows, by the point's
/// number (see shapeAnchors); a point with none stays where it is.
using ShapeBinding = std::vector<std::vector<BoneWeight>>;

/// A bone's spring, as a node may carry one: how strongly it pulls back to
/// its pose and how quickly that dies away. Kept, not simulated.
struct BoneSpring
{
    bool present = false;
    double strength = 0.0;
    double damping = 0.0;
};

/// An IK armature's pose span, read from and written back to the IKTree
/// Animate keeps on a pose frame (as XML text).
///
/// Animate keeps a pose as a State on every node: the node's angle relative
/// to its parent's, and its offset in the parent's frame, in twips (absolute,
/// for the root's child). An IKState for each pose gives its length and the
/// ease to the next one. Every frame of the span then has its joints worked
/// out and stored on the node, as xArray and yArray (twips) and angleArray
/// (radians, absolute): the poses' angles and offsets eased from each pose to
/// the next, a simple quadratic ease of strength s running time as
/// (1 - s) t + s (2t - t^2), and the joints placed down the tree from them.
/// Free of Qt.
class IKArmature
{
public:
    IKArmature();
    ~IKArmature();

    IKArmature(const IKArmature& other);
    IKArmature& operator=(const IKArmature& other);

    /// Reads an IKTree. False when \a xml is not one or has no posed joints.
    bool parse(const std::string& xml);

    /// The IKTree as it now stands.
    std::string xml() const;

    /// Whether the armature bends a shape (its joints are "lsd" nodes), as
    /// opposed to joining symbol instances.
    bool bendsShape() const { return _bendsShape; }

    /// Frames in the span.
    int frames() const;

    /// The referenceID of the symbol instance a joint moves, in an armature
    /// of symbols; empty when it has none.
    std::string referenceOf(int joint) const;

    /// The frames, into the span, where poses are.
    std::vector<int> poses() const;

    /// Each pose's ease to the next (its IKState's easeType and easeValue)
    /// made what \a from has for the pose starting on the same frame.
    void copyEases(const IKArmature& from);

    /// The joints as stored for \a frame frames into the span.
    std::vector<IKJoint> jointsAt(int frame) const;

    /// The joints the art shows \a frame frames into the span. In a shape's
    /// armature, the stored joints read at the eased entry ikBones reads
    /// (see ik_tree.h), where the shapes Animate stores for each frame have
    /// them; in an armature of symbols, the stored joints themselves, which
    /// its instances follow (see carried).
    std::vector<IKJoint> shownAt(int frame) const;

    /// Where the instance a joint of an armature of symbols moves shows \a
    /// frame frames into the span, given \a stored, its matrix as saved,
    /// which is where the span's first frame has it. It turns with its joint
    /// and keeps its place from it: the stored registration point and angle
    /// (xArray, yArray, angleArray) of the frame against the first's.
    Transform carried(int joint, const Transform& stored, int frame) const;

    /// The betweenFrameMatrixList of an armature of symbols: for every
    /// frame of the span, each joint's instance (\a instances, by joint; a
    /// null leaves one out) as it sits in its joint's frame, its matrix with
    /// the joint's turn and registration point taken off. The instances
    /// keep that place on every frame, so the list repeats it.
    std::string betweenFrameMatrixList(const std::vector<const Transform*>& instances) const;

    /// The joints \a frame frames into the span as the poses make them.
    std::vector<IKJoint> posedAt(int frame) const;

    /// Poses the armature at \a frame with every joint at the absolute angle
    /// given, adding a pose there when there is none, and works every
    /// frame's joints out again. Offsets stay as they were. False when the
    /// angles do not fit the joints or the frame is outside the span.
    bool setPose(int frame, const std::vector<double>& angles);

    /// Makes the span \a count frames long, the last pose held longer or
    /// cut shorter, and works every frame's joints out again. False when
    /// that would leave the last pose no frames.
    bool setFrames(int count);

    /// Each joint's rotation limits.
    std::vector<JointLimit> limits() const;
    void setLimit(int joint, const JointLimit& limit);

    /// The armature's name ("Armature_2") and tree name, as the IKTree
    /// gives them.
    std::string name() const;
    std::string treeName() const;

    /// An armature's joints as its first frame has them, for building it
    /// again with a bone more (see armatureXml).
    std::vector<ArmatureJoint> symbolJoints() const;

    /// A joint's spring, read from its node (springStrength and
    /// springDamping, or strength and damping).
    BoneSpring spring(int joint) const;

    /// Copies onto this armature's nodes, matched by name, the attributes
    /// of \a other's that this one's lack (springs, limits and the rest
    /// Phoenix does not model), and the tree's own. Used when an armature is
    /// built again with a bone more, so nothing is lost.
    void keepAttributesOf(const IKArmature& other);

    /// The bones each point of a boned shape follows, read from the tree's
    /// connectionIndices and connectionData; empty when it has none.
    ShapeBinding binding() const;

    /// Whether the binding numbers its points as shapeAnchors does, as
    /// Phoenix writes it (phoenixBinding="anchors"), so a shape's points can
    /// be found in it.
    bool bindsAnchors() const;

    /// Writes \a binding as connectionIndices and connectionData, and
    /// \a anchors (the shape's points, in the armature's space as the
    /// span's first frame has them) as the IKContour, as Animate keeps
    /// them: for each point, each of its bones with the point's place along
    /// the bone (0 at its head, 1 at its tail), its distance to the side
    /// (signed) and from the bone, and its weight. \a contour is the
    /// IKContour's matrix (the ik container's placement); \a style, the XML
    /// of the outline's stroke and fill, kept as given.
    void setBinding(const ShapeBinding& binding, const std::vector<Point>& anchors, const Transform& contour,
        const std::string& style = std::string());

private:
    struct Node
    {
        tinyxml2::XMLElement* element = nullptr;
        int parent = -1;
        /// In an armature of symbols, where the instance's registration
        /// point sits from the joint, in the joint's frame: xArray holds the
        /// registration point there, not the joint.
        Point registration;
    };

    struct Pose
    {
        double angle = 0.0;
        double x = 0.0;
        double y = 0.0;
    };

    void index();
    /// A joint's stored registration point (pixels) and angle \a frame
    /// frames into the span, as xArray, yArray and angleArray keep them.
    bool storedAt(int joint, int frame, Point& registration, double& angle) const;
    std::vector<tinyxml2::XMLElement*> states(const tinyxml2::XMLElement* owner, const char* name) const;
    /// A node's relative angle and offset (twips) at \a frame.
    Pose poseOf(size_t node, int frame) const;
    double easedTime(int pose, double t) const;
    void storeJoints();

    std::unique_ptr<tinyxml2::XMLDocument> _document;
    std::vector<Node> _nodes;
    bool _bendsShape = false;
};

/// Turns the joints above \a effector so it reaches for \a target, by cyclic
/// coordinate descent from the effector's parent up to the root, as Animate's
/// selection tool drags a bone's end. A joint with a limit in \a limits
/// turns no further than it allows. Returns the joints posed.
std::vector<IKJoint> solveIK(std::vector<IKJoint> joints, int effector, const Point& target, int iterations = 16,
    const std::vector<JointLimit>& limits = {});

/// The IKTree text of a new armature of symbol instances, one pose \a
/// frames long, posed as \a joints are (parents before children), as
/// Animate writes one: each joint turned toward its first child joint.
std::string symbolArmatureXml(const std::string& name, const std::string& treeName,
    const std::vector<ArmatureJoint>& joints, int frames = 1);

/// The IKTree text of a new armature, of symbol instances or, when \a
/// bendsShape, bending a shape: then its nodes are "lsd" nodes, xArray and
/// yArray hold the joints themselves and each node has its worldMatrix, as
/// in a boned shape Animate saves. A shape's binding is set afterwards
/// (IKArmature::setBinding).
std::string armatureXml(const std::string& name, const std::string& treeName, const std::vector<ArmatureJoint>& joints,
    int frames, bool bendsShape);

/// \a rebuilt, an armature made again with a joint more from \a old's first
/// frame's joints, posed as \a old was at each of its other poses: every
/// joint \a old has at its angle there (matched by node name), and the new
/// one keeping the turn from its parent it has on the first frame.
void carryPoses(const IKArmature& old, IKArmature& rebuilt);

/// The control points of a shape, in its own space, numbered in the order
/// its edges reach them: each anchor once, however many edges meet there.
/// Bending a shape keeps its edges, so its points keep their numbers.
std::vector<Point> shapeAnchors(const Shape& shape);

/// The stroke and fill an IKContour carries for \a shape: its first
/// stroke's and fill's, when they are solid colors.
std::string contourStyle(const Shape& shape);

/// Binds each point to its nearest bone, as the Bone tool does: a point as
/// near one bone as another (at the joint they share, say) follows both,
/// evenly. \a anchors and \a joints are in the armature's space.
ShapeBinding bindToNearestBones(const std::vector<Point>& anchors, const std::vector<IKJoint>& joints);

/// Bends \a shape by its binding as the joints move from \a before to \a
/// after: each point is carried rigidly by the bones it follows, averaged
/// by weight, and a curve's control point between two points by both of
/// theirs. \a toArmature takes the shape's own space to the armature's.
/// Does nothing when the shape's points are not the binding's.
void bindShape(Shape& shape, const Transform& toArmature, const ShapeBinding& binding,
    const std::vector<IKJoint>& before, const std::vector<IKJoint>& after);

/// Where \a point (in the armature's space) goes when the joints move from
/// \a before to \a after: each bone carries it rigidly with its parent
/// joint's frame, weighted by how near the bone it lies.
Point skinPoint(const Point& point, const std::vector<IKJoint>& before, const std::vector<IKJoint>& after);

/// Bends \a shape the way skinPoint moves its points; \a toArmature takes
/// the shape's own space to the armature's.
void reskinShape(Shape& shape, const Transform& toArmature, const std::vector<IKJoint>& before,
    const std::vector<IKJoint>& after);

/// A new armature layer for \a timeline, to go above \a below: empty up to
/// \a start, then one pose frame \a length frames long, which \a pose is set
/// to. The caller owns it until an AddLayerCommand takes it.
Layer* newArmatureLayer(Timeline& timeline, const Layer& below, const std::string& name, int start, int length,
    Frame*& pose);

/// A shape boned by the Bone tool, made but not yet in the document.
struct BonedShape
{
    /// The ik container symbol: the shape on every frame of the span, about
    /// its middle. Owned by the caller until the library takes it.
    Symbol* symbol = nullptr;
    /// The new armature layer, its pose frame holding the container's
    /// instance and the IKTree, its points bound to their nearest bones.
    Layer* layer = nullptr;
    Frame* pose = nullptr;
    SymbolInstance* instance = nullptr;
};

/// Bones \a shape, on \a shapeFrame of \a layer, with \a joints (in the
/// timeline's space; parents before children): the shape is copied into a
/// new ik container symbol named \a symbolName with a frame for each of the
/// span's, and an armature layer named \a armatureName holds it. The shape
/// itself is left for the caller to take off its frame.
BonedShape boneShape(Timeline& timeline, const Layer& layer, const Frame& shapeFrame, const Shape& shape,
    const std::vector<ArmatureJoint>& joints, const std::string& symbolName, const std::string& armatureName,
    const std::string& treeName);

/// Puts \a list (a betweenFrameMatrixList) among a pose frame's children,
/// in place of the one there or else before its IKTree.
void setBetweenFrames(std::vector<std::string>& children, const std::string& list);

/// Swaps a pose frame's IKTree text, one of its extraChildren, and the pose
/// list it keeps in its poseLocations attribute. The shapes the armature
/// bends change in their own commands alongside.
class PoseArmatureCommand : public Command
{
public:
    PoseArmatureCommand(Frame* frame, size_t child, const std::string& before, const std::string& after,
        const std::string& posesBefore, const std::string& posesAfter);

    void redo() override;
    void undo() override;
    std::string name() const override { return "Pose Armature"; }

    /// Writes \a xml and \a poses into \a frame, as the command does.
    static void apply(Frame* frame, size_t child, const std::string& xml, const std::string& poses);

private:
    Frame* _frame;
    size_t _child;
    std::string _before;
    std::string _after;
    std::string _posesBefore;
    std::string _posesAfter;
};

} // namespace fla
