#pragma once

#include <vector>

namespace fla {

class DOMElement;
class Element;
class Frame;
class Layer;

/// Drawing inbetweens for frame-by-frame animation: a new keyframe whose
/// drawing lies part way between the drawings of two keyframes, made with
/// the shape tween's own morph (and its shape hints) and left as an
/// ordinary, editable keyframe. Free of Qt.
namespace inbetween {

/// How the drawings of Fill Inbetweens are spaced: evenly, bunched toward
/// the first key (easing in), or bunched toward the second (easing out).
enum class Spacing
{
    Even,
    EaseIn,
    EaseOut
};

/// What an inbetween was made of: shapes morphed, symbol instances (and
/// groups and text) moved part way, and things with no partner on the
/// other key, held as the nearer key draws them.
struct Report
{
    int morphed = 0;
    int moved = 0;
    int held = 0;
    /// Keyframes written.
    int keyframes = 0;

    Report& operator+=(const Report& other);
};

/// How far between the keys the drawing of step \a step of \a steps lies,
/// 0 to 1, spaced as \a spacing says.
double spacingAt(Spacing spacing, int step, int steps);

/// The drawing \a t of the way from \a from's to \a to's: the shapes pair
/// up in stacking order and each pair is morphed as a shape tween would
/// (with the hints kept on \a from); instances, groups and text pair as a
/// classic tween pairs them and move, turn, scale and change color part
/// way. Whatever has no partner is copied from the nearer key. The copies
/// belong to \a parent; the caller owns them.
std::vector<Element*> drawingBetween(const Frame& from, const Frame& to, double t, DOMElement* parent,
    Report& report);

/// The two keyframes an inbetween goes between, given the frames \a first
/// to \a last selected: the keyframe whose span holds \a first and the
/// keyframe after it. Standing on the last keyframe, the one before it
/// and that one. False when there are not two.
bool keysAround(Layer& layer, int first, int last, Frame*& from, Frame*& to);

/// Insert Inbetween: a keyframe \a t of the way between the keys around
/// the selection, placed at the frame that far between them. When the keys
/// are on frames next to each other, a frame is inserted after the first
/// to make room. Returns the new keyframe's index, or -1.
int insertInbetween(Layer& layer, int first, int last, double t, Report& report);

/// Fill Inbetweens: every frame between the keys around the selection
/// becomes a keyframe with its own inbetween drawing, spaced as asked. A
/// tween on the first key goes, its frames now drawn. Returns how many
/// keyframes were made.
int fillInbetweens(Layer& layer, int first, int last, Spacing spacing, Report& report);

} // namespace inbetween

} // namespace fla
