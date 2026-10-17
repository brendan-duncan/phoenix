#pragma once

#include <string>
#include <vector>

namespace fla {

class Bitmap;
class Document;
class Element;
class Timeline;

/// Publishing to HTML5: a document as the JavaScript runtime in
/// resources/html5 plays it. This part walks the document into the movie's
/// content (resources/html5/README.md, "Content"): its symbols and their
/// timelines, each frame the list of what it shows in depth order (tweens
/// worked out frame by frame), its text fields, frame labels, sounds,
/// linkage names and, for an HTML5 Canvas document, its frame scripts. What
/// the art looks like comes from an ArtSource, which the app draws with its
/// own renderer. Free of Qt.
namespace html5 {

/// Draws the art the content refers to, and keeps the files it needs.
class ArtSource
{
public:
    virtual ~ArtSource() = default;
    /// \a element (a shape, group, primitive, bitmap or static text) as
    /// it draws in its own space, without its own transform (a group in
    /// its parent's, as its members carry their own): a published shape's
    /// JSON ({"o": [...], "b": [x0, y0, x1, y1]}), or empty when it draws
    /// nothing.
    virtual std::string art(const Element& element) = 0;
    /// The picture of a library bitmap, as an index into images(); -1 when
    /// it has none.
    virtual int bitmap(const Bitmap& bitmap) = 0;
    /// The library sound \a name as an index into sounds(); -1 when it
    /// cannot be written.
    virtual int sound(const std::string& name) = 0;
    /// What Phoenix draws of layers \a from to \a to - 1 of \a timeline on
    /// its frame \a frame (from 0), compositing and all, as a published
    /// shape's JSON holding the one picture, placed in the timeline's
    /// space; empty when nothing shows or it cannot be drawn. For what the
    /// runtime has nothing like (Phoenix's own compositing): it goes out as
    /// a picture a frame, frames that look alike sharing one.
    virtual std::string picture(const Timeline& timeline, int from, int to, int frame)
    {
        (void)timeline;
        (void)from;
        (void)to;
        (void)frame;
        return std::string();
    }
    /// The JSON arrays of the pictures and sounds handed out so far.
    virtual std::string images() = 0;
    virtual std::string sounds() = 0;
};

/// A run of a timeline's layers (\a from to \a to - 1, from the top) that
/// Phoenix's own compositing draws as one, which the runtime has nothing
/// like, so it is published as pictures: what it is, for the notes.
struct BakedRun
{
    int from = 0;
    int to = 0;
    std::string why;
};

/// The runs of \a timeline's layers published as pictures: a node graph's
/// whole timeline; an adjustment layer or a layer mode (Stencil,
/// Silhouette, Preserve Transparency) and every layer under it; an effect
/// layer and the layer it works on; a track matte and its matte; a mask
/// with a feathered, grown or inverted edge, or a layer under several
/// masks, with the masks and all they mask; motion blur, echo and posterize
/// time; shape operators; effects on instances that no filter carries; the
/// layers with instances turned in 3D.
std::vector<BakedRun> bakedRuns(const Timeline& timeline);

/// A movie's ActionScript 1 and 2, gathered while its content is made, for
/// translating to JavaScript (fla::html5as2): each keyframe's script, and
/// each instance's on() and onClipEvent() handlers, which the content's
/// entry for the instance names by its id ("h").
struct ActionScript2
{
    struct FrameScript
    {
        /// "" for the main timeline, else the symbol's library name.
        std::string timeline;
        /// From 0; on the main timeline counted through its scenes.
        int frame = 0;
        std::string code;
        /// The main timeline's scene the frame is in.
        std::string scene;
        std::string where;
    };
    struct InstanceScript
    {
        int id = 0;
        std::string code;
        std::string where;
    };
    std::vector<FrameScript> frames;
    std::vector<InstanceScript> instances;
};

struct Options
{
    /// The movie's scripts are JavaScript (an HTML5 Canvas document): its
    /// frame scripts go into the content.
    bool javaScript = false;
    /// The main timeline stops at its end rather than looping.
    bool loop = true;
    /// Every symbol, not only those the scenes show and those exported for
    /// ActionScript.
    bool allSymbols = false;
    /// The movie's ActionScript goes out translated to JavaScript beside the
    /// content (Html5Publish), so its frame scripts are not left out.
    bool actionScriptTranslated = false;
    /// Where the movie's ActionScript 1 and 2 goes, for a movie whose
    /// scripts are; null leaves them out (with a note).
    ActionScript2* actionScript2 = nullptr;
};

/// The content of \a document as JSON. \a notes gets what could not be
/// published as it is.
std::string content(const Document& document, ArtSource& source, const Options& options, std::vector<std::string>* notes);

/// \a value as JavaScript string literal text, quotes and all.
std::string quoted(const std::string& value);

/// \a value with at most \a decimals places, trailing zeros dropped.
std::string number(double value, int decimals = 3);

/// The document class the publish settings name, or empty.
std::string documentClass(const Document& document);

} // namespace html5

} // namespace fla
