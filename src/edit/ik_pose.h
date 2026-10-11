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
class Shape;

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

    /// The joints as stored for \a frame frames into the span.
    std::vector<IKJoint> jointsAt(int frame) const;

    /// The joints the art shows \a frame frames into the span: the stored
    /// joints read at the eased entry ikBones reads (see ik_tree.h), where
    /// the shapes Animate stores for each frame have them.
    std::vector<IKJoint> shownAt(int frame) const;

    /// The joints \a frame frames into the span as the poses make them.
    std::vector<IKJoint> posedAt(int frame) const;

    /// Poses the armature at \a frame with every joint at the absolute angle
    /// given, adding a pose there when there is none, and works every
    /// frame's joints out again. Offsets stay as they were. False when the
    /// angles do not fit the joints or the frame is outside the span.
    bool setPose(int frame, const std::vector<double>& angles);

private:
    struct Node
    {
        tinyxml2::XMLElement* element = nullptr;
        int parent = -1;
    };

    struct Pose
    {
        double angle = 0.0;
        double x = 0.0;
        double y = 0.0;
    };

    void index();
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
/// selection tool drags a bone's end. Returns the joints posed.
std::vector<IKJoint> solveIK(std::vector<IKJoint> joints, int effector, const Point& target, int iterations = 16);

/// Where \a point (in the armature's space) goes when the joints move from
/// \a before to \a after: each bone carries it rigidly with its parent
/// joint's frame, weighted by how near the bone it lies.
Point skinPoint(const Point& point, const std::vector<IKJoint>& before, const std::vector<IKJoint>& after);

/// Bends \a shape the way skinPoint moves its points; \a toArmature takes
/// the shape's own space to the armature's.
void reskinShape(Shape& shape, const Transform& toArmature, const std::vector<IKJoint>& before,
    const std::vector<IKJoint>& after);

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
