#pragma once

#include <string>
#include <vector>

namespace fla {
class CommandStack;
class DrawingStyle;
class Element;
class FLADocument;
class Selection;
class Timeline;
} // namespace fla

namespace jsfl {

/// A rectangle in a timeline's coordinates.
struct Rect
{
    double left = 0.0;
    double top = 0.0;
    double right = -1.0;
    double bottom = -1.0;
    bool valid() const { return right >= left && bottom >= top; }
    double width() const { return valid() ? right - left : 0.0; }
    double height() const { return valid() ? bottom - top : 0.0; }
};

/// A block of selected frames: frames first to last (counting from 0, last
/// included) of the layer at \a layer.
struct FrameRange
{
    int layer = 0;
    int first = 0;
    int last = 0;
};

/// What the object model needs from the application running the script:
/// the open documents and their undo histories, where editing happens, the
/// pictures and bounds only drawing can give, and how to talk to the person.
/// The window implements it (script_runner.cpp); so do the tests.
class Host
{
public:
    virtual ~Host() = default;

    // Output and dialogs. A host with no one to ask (the command line)
    // prints alerts, confirms and answers prompts with their default.
    virtual void trace(const std::string& text) = 0;
    virtual void clearOutput() {}
    /// Everything traced so far, for fl.outputPanel.save.
    virtual std::string outputText() { return {}; }
    virtual void alert(const std::string& message) = 0;
    virtual bool confirm(const std::string& message) = 0;
    /// False when the person cancels.
    virtual bool prompt(const std::string& message, const std::string& initial, std::string& answer) = 0;
    /// A file the person picks ("open" or "save"), as a platform path;
    /// empty when canceled.
    virtual std::string browseForFile(const std::string& mode, const std::string& title)
    {
        (void)mode;
        (void)title;
        return {};
    }
    virtual std::string browseForFolder(const std::string& title)
    {
        (void)title;
        return {};
    }
    /// The folder holding the user's Commands folder and other script
    /// settings, as a platform path.
    virtual std::string configDirectory() = 0;
    /// Phoenix's version, "0.6.0".
    virtual std::string version() { return {}; }

    // Documents.
    virtual std::vector<fla::FLADocument*> documents() = 0;
    /// The document shown, or null with none open.
    virtual fla::FLADocument* activeDocument() = 0;
    virtual void activate(fla::FLADocument* document) = 0;
    /// Opens the document at \a path (or shows it, when it is open
    /// already); null with \a error when it cannot.
    virtual fla::FLADocument* open(const std::string& path, std::string& error) = 0;
    virtual fla::FLADocument* create(std::string& error) = 0;
    /// Writes \a document to \a path, or to its own file when \a path is
    /// empty. A copy leaves the document's own file and saved state alone.
    virtual bool save(fla::FLADocument* document, const std::string& path, bool copy, std::string& error) = 0;
    /// False when the person kept it open.
    virtual bool close(fla::FLADocument* document, bool askToSave) = 0;
    /// Its file as a platform path, empty when never saved.
    virtual std::string path(fla::FLADocument* document) = 0;
    virtual std::string name(fla::FLADocument* document) = 0;
    virtual bool modified(fla::FLADocument* document) = 0;
    /// A document from a newer Phoenix, which must not change.
    virtual bool readOnly(fla::FLADocument* document)
    {
        (void)document;
        return false;
    }

    // Editing.
    virtual fla::CommandStack& history(fla::FLADocument* document) = 0;
    virtual fla::Selection& selection(fla::FLADocument* document) = 0;
    /// The document's content changed: drop what was worked out from it.
    virtual void changed(fla::FLADocument* document) { (void)document; }
    /// Everything shown brought up to date, once a script is done.
    virtual void refresh() {}

    // Where editing happens: the scene or symbol being edited, the
    // playhead (from 0) and the current layer (from 0 at the top).
    virtual fla::Timeline* editedTimeline(fla::FLADocument* document) = 0;
    virtual void editTimeline(fla::FLADocument* document, fla::Timeline* timeline) = 0;
    virtual int currentFrame(fla::FLADocument* document) = 0;
    virtual void setCurrentFrame(fla::FLADocument* document, int frame) = 0;
    virtual int currentLayer(fla::FLADocument* document) = 0;
    virtual void setCurrentLayer(fla::FLADocument* document, int layer) = 0;
    /// The frames the person selected in the timeline before the script
    /// ran.
    virtual std::vector<FrameRange> selectedFrames(fla::FLADocument* document)
    {
        (void)document;
        return {};
    }

    // Drawing.
    /// The fill and stroke new shapes get, and whether they are drawing
    /// objects; null for Phoenix's defaults.
    virtual const fla::DrawingStyle* drawingStyle() { return nullptr; }
    /// \a element's bounds in its timeline, as the stage draws it.
    virtual Rect bounds(fla::FLADocument* document, const fla::Element* element) = 0;
    /// The current frame (or every frame, numbered) of the document as PNG.
    virtual bool exportPNG(fla::FLADocument* document, const std::string& path, bool currentFrameOnly,
        std::string& error) = 0;
};

} // namespace jsfl
