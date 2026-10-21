#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Document;
class Layer;
class Timeline;

/// Master controllers: one slider driving several
/// properties at once (a head turn moving the head, swapping the eyes'
/// drawing and turning the nose). Moving a controller sets the properties
/// it drives on the frame shown, keying the layers there, so what is saved
/// is ordinary keyframes any player plays as they are. The controllers
/// themselves are kept in the document where other apps that open the
/// FLA ignore them. Free of Qt.
namespace controllers {

/// What a controller drives: a property of the symbol instances on a
/// layer, between two values as the controller goes from 0 to 1.
struct Binding
{
    /// "scene:N" for a scene's layer, "symbol:Name" for one inside a symbol.
    std::string timeline;
    std::string layer;
    /// x, y, rotation, scale, scaleX, scaleY, alpha (percent), frame (a
    /// graphic's first frame, from 1) or, for a joystick, frameGrid.
    std::string property;
    double from = 0.0;
    double to = 0.0;
    /// A joystick's axis that drives it: 'x' (from at the left, to at the
    /// right) or 'y' (from at the top, to at the bottom). A slider's
    /// bindings all follow its one value.
    char axis = 'x';
    /// For a joystick's frameGrid: a graphic's drawings from frame \a from
    /// to \a to laid out in rows of this many, the handle picking the one
    /// under it (a head drawn turned left to right across each row and up
    /// to down from row to row). Both axes drive it.
    int columns = 0;
};

struct Controller
{
    std::string name;
    /// A slider's value, or a joystick's across, both 0 to 1.
    double value = 0.0;
    std::vector<Binding> bindings;
    /// A joystick: a handle dragged about a square, its across and down
    /// each driving the bindings on that axis (a head turning left and
    /// right, and up and down, at once).
    bool joystick = false;
    /// A joystick's down, 0 at the top to 1 at the bottom.
    double valueY = 0.5;
};

/// The properties a binding can drive, in menu order.
const std::vector<std::string>& properties();
/// The same for a joystick's bindings, with frameGrid.
const std::vector<std::string>& joystickProperties();

/// The controllers kept in \a document.
std::vector<Controller> load(const Document& document);
/// \a document's attributes with \a controllers kept in them.
std::vector<std::pair<std::string, std::string>> saved(const Document& document, const std::vector<Controller>& controllers);

/// The timeline a binding names, in \a document.
Timeline* timelineOf(Document& document, const std::string& name);
/// The name a binding gives \a timeline of \a document.
std::string nameOf(const Document& document, const Timeline* timeline);

/// The value \a binding gives its property at controller value \a value.
double valueAt(const Binding& binding, double value);
/// The value \a binding gives its property with a joystick at \a x, \a y
/// (a slider's binding takes \a x).
double valueAt(const Controller& controller, const Binding& binding, double x, double y);

/// The property's value on the first symbol instance of \a layer's frame
/// \a frame, for a binding's starting values; false when there is none.
bool currentValue(const Layer& layer, int frame, const std::string& property, double& value);

/// Sets \a controller to \a value on frame \a frame: every layer it drives
/// gets a keyframe there (made when there is none) and its instances the
/// values. One undoable step; null when it drives nothing that is there.
CommandPtr apply(Document& document, const Controller& controller, double value, int frame);
/// The same for a joystick put at \a x, \a y.
CommandPtr apply(Document& document, const Controller& controller, double x, double y, int frame);

} // namespace controllers

} // namespace fla
