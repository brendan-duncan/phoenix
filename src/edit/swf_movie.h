#pragma once

#include "../data/color_transform.h"
#include "../data/filter.h"
#include "../util/swf_writer.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class Document;
class Frame;
class Layer;
class StaticText;
class Symbol;
class SymbolInstance;
class Timeline;

/// A document written as an SWF movie with its structure, so the movie's
/// ActionScript runs in any player: the main timeline's scenes one after
/// another, movie clips as sprites with timelines of their own, buttons with
/// their states and handlers, instance names, color effects, filters and
/// blend modes on what is placed, masks as clipping layers, frame labels,
/// dynamic and input text as text fields, and the scripts compiled to
/// action bytecode: frame scripts, on() and onClipEvent() handlers,
/// #initclip, ActionScript 2 classes and the symbols linked to them.
///
/// The art (shapes, text, bitmaps, graphic symbols) is drawn as the rest of
/// Phoenix draws it, by a Drawer, and written as shapes. A graphic symbol
/// with movie clips or buttons in it has its frames placed into the
/// timeline it is on, frame by frame as it loops or plays once, so what is
/// in it stays a placed clip scripts reach by name. Motion tweens go in as
/// a placement a frame; shape tweens as morph shapes where the tween's
/// fills and lines can be, else as a shape a frame. Sounds that play as
/// events start on the timelines they are on, and buttons sound their
/// states' sounds. Free of Qt.
namespace swfmovie {

/// One element of a layer's frame as the movie has it.
struct Piece
{
    /// Its place among the frame's elements.
    int element = -1;
    /// Where it is inside the graphics placed into the timeline (each
    /// level's layer, bottom first, and element), empty for an element of
    /// the layer itself. A piece in a graphic is placed by its matrix and
    /// color, art too, and then by \a within: the graphics' colors.
    std::vector<int> path;
    swf::ColorTransform within;
    /// A graphic symbol whose frames go into the timeline (see holdsClips):
    /// the frame of its own it shows, or -1.
    int graphicFrame = -1;
    /// A shape tween's morph shape and how far it has run (0 to 65535).
    int morph = 0;
    int ratio = -1;
    /// Art: shapes in the writer, in drawing order, each drawn with an
    /// alpha and a blend mode. No shapes draws nothing.
    struct Shape
    {
        int id = 0;
        double alpha = 1.0;
        swf::Writer::Blend blend = swf::Writer::Blend::Normal;
    };
    std::vector<Shape> shapes;
    /// Or a movie clip or button the movie places, or a dynamic or input
    /// text field: where it is in its timeline's space and how it shows
    /// there on the frame (a tween's in-between), for the player to draw.
    const SymbolInstance* instance = nullptr;
    const StaticText* text = nullptr;
    swf::Matrix matrix;
    ColorTransform color;
    std::vector<Filter> filters;
    std::string blendMode;
};

/// Draws the art: what a layer shows on a frame, element by element.
class Drawer
{
public:
    virtual ~Drawer() = default;
    /// The pieces \a layer shows on frame \a frame (from 0) of its
    /// timeline, in drawing order, their shapes written to \a writer. Movie
    /// clips and buttons placed directly on the layer, and dynamic text
    /// fields with a name or a variable and input fields, are pieces of
    /// their own, and so are graphics that hold such things (holdsClips),
    /// with the frame they show; other graphics and the rest are art.
    virtual std::vector<Piece> layerFrame(const Layer& layer, int frame, swf::Writer& writer) = 0;
    /// Whether \a layer of \a timeline shows: on the main timeline its eye
    /// hides it, in a symbol it shows (as Phoenix's view draws them).
    virtual bool shows(const Timeline& timeline, const Layer& layer) const;
};

/// Why \a document cannot go out with its structure (one of Phoenix's own
/// effects, behaviors, 3D, cameras... that an SWF has no place for), or
/// empty when it can.
std::string unstructured(const Document& document);

/// Whether \a document has scripts: frame scripts or handlers on instances.
bool hasScripts(const Document& document);

/// The ActionScript 2 classes \a document's scripts and linkage reach, by
/// qualified name, with their sources: the names its scripts mention that
/// \a findClass finds on the class path, and the classes those mention.
std::map<std::string, std::string> actionScript2Classes(const Document& document,
    const std::function<std::optional<std::string>(const std::string& qualifiedName)>& findClass);

/// A dynamic or input text field's instance name, or empty.
std::string fieldName(const StaticText& text);

/// Whether a dynamic or input field goes in as a text field: an input
/// field, or a dynamic one with a name or a variable.
bool scriptedField(const StaticText& text);

/// Whether graphic symbol \a symbol has movie clips, buttons or text fields
/// scripts reach in it (in its graphics too), and no masks: its frames go
/// into the timelines it is placed on rather than being drawn as art.
bool holdsClips(const Symbol& symbol);

struct Options
{
    /// An ActionScript 2 class's source by its qualified name, or none: the
    /// document's class path, as Test Movie finds classes.
    std::function<std::optional<std::string>(const std::string& qualifiedName)> findClass;
    /// The movie's size over the document's: the main timeline's content is
    /// scaled by it.
    double scaleX = 1.0;
    double scaleY = 1.0;
    /// An ActionScript 3 movie (its publish settings say version 3) has its
    /// scripts written by this, as DoABC and SymbolClass tags (see
    /// swf::Writer::doABC), once the movie clips with frame scripts, a
    /// linked class or components, and the buttons with a linked class,
    /// are written: \a sprites gives their character ids (see
    /// writeActionScript3). The movie is marked ActionScript 3 first, and
    /// its components are placed as clips of their symbols. Without it, an
    /// ActionScript 3 movie goes out without its scripts.
    std::function<void(swf::Writer& writer, const std::map<const Symbol*, int>& sprites)> actionScript3;
    /// A keyframe's sound that plays as an event (event, start or stop):
    /// its sound's id, written to the movie the first time, and how it
    /// plays; false when the keyframe has none, or it cannot go in. The
    /// sounds start on the timelines their keyframes are on (StartSound),
    /// and buttons' state frames give their sounds (DefineButtonSound).
    /// Without it, the movie has none.
    std::function<bool(const Frame& key, int& id, swf::SoundInfo& info)> eventSound;
};

class Movie
{
public:
    Movie(const Document& document, swf::Writer& writer, Drawer& drawer, const Options& options = Options());
    ~Movie();

    /// Writes the movie: the classes and the symbols scripts reach by
    /// linkage, then the main timeline frame by frame, its scenes one after
    /// another. \a beforeShow is called on each of the main timeline's
    /// frames before it is shown (for the sounds); false from it stops.
    /// The frames written.
    int write(const std::function<bool(int frame)>& beforeShow = std::function<bool(int)>());

    /// The frame (from 0) of the main timeline \a scene starts on.
    int sceneStart(const Timeline* scene) const;

    /// What did not go in as it is, a line each: scripts that do not
    /// compile, a symbol inside itself.
    const std::vector<std::string>& problems() const;

    struct State;

private:
    std::unique_ptr<State> _state;
};

/// The SWF color transform an instance's color effect is: tint,
/// brightness and the advanced settings as one multiply and add.
swf::ColorTransform colorTransform(const ColorTransform& color);
/// The SWF filters for \a filters (those switched on).
std::vector<swf::Filter> filters(const std::vector<Filter>& filters);
/// A blend mode named as the file names it, as SWF numbers it (1 normal).
int blendMode(const std::string& name);

} // namespace swfmovie

} // namespace fla
