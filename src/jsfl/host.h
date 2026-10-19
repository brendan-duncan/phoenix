#pragma once

#include "../data/transform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {
class CommandStack;
class DrawingStyle;
class Element;
class FLADocument;
class Selection;
class SymbolInstance;
class Timeline;
} // namespace fla

namespace jsfl {

namespace xul {
class Dialog;
}

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
    /// Shows an XML (XUL) dialog until the person closes it, telling
    /// \a dialog what they change and close. False when there is no one to
    /// show it to; the dialog is then accepted as it stands.
    virtual bool showDialog(xul::Dialog& dialog)
    {
        (void)dialog;
        return false;
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
    /// Edits \a instance's symbol inside the instance, where it stands on
    /// the stage, as double-clicking it does.
    virtual void editInPlace(fla::FLADocument* document, fla::SymbolInstance* instance);
    /// From the space of what is being edited to the document's: through
    /// every instance entered in place. Identity at a scene.
    virtual fla::Transform viewMatrix(fla::FLADocument* document)
    {
        (void)document;
        return fla::Transform();
    }
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
    virtual fla::DrawingStyle* drawingStyle() { return nullptr; }
    /// Puts the text on the system clipboard.
    virtual void copyText(const std::string& text) { (void)text; }

    // The toolbox and the view.
    /// The tools by the names the toolbox gives them ("Selection", "Oval",
    /// a JSFL tool's own name), in toolbox order.
    virtual std::vector<std::string> tools() { return {}; }
    /// The tool in use, by its toolbox name; empty when none is known.
    virtual std::string activeTool() { return {}; }
    /// Picks the tool of toolbox name \a name; false when there is none.
    virtual bool selectTool(const std::string& name)
    {
        (void)name;
        return false;
    }
    /// Reads the Tools folder again, adding the tools put there since.
    virtual void reloadTools() {}
    /// Whether Test Movie can play \a document now.
    virtual bool canTestMovie(fla::FLADocument* document)
    {
        (void)document;
        return false;
    }
    /// Runs one of Phoenix's own commands on \a document: "testMovie" (on
    /// the document as it is, the script waiting until its window closes),
    /// or "convertLinesToFills" and "convertSelectionToBitmap" (on the
    /// selection). False with \a error
    /// when this host cannot.
    virtual bool runCommand(fla::FLADocument* document, const std::string& name, std::string& error)
    {
        (void)document;
        error = name == "testMovie" ? "Test Movie needs the Phoenix window." : name + " needs the Phoenix window.";
        return false;
    }
    /// The stage's magnification for \a document (1 is 100%), and setting
    /// it.
    virtual double zoom(fla::FLADocument* document)
    {
        (void)document;
        return 1.0;
    }
    virtual void setZoom(fla::FLADocument* document, double zoom)
    {
        (void)document;
        (void)zoom;
    }
    /// \a element's bounds in its timeline, as the stage draws it.
    virtual Rect bounds(fla::FLADocument* document, const fla::Element* element) = 0;
    /// The pixels of library bitmap \a item, straight RGBA, top row first;
    /// false when it cannot be read.
    virtual bool bitmapPixels(fla::FLADocument* document, const std::string& item, int& width, int& height,
        std::vector<uint8_t>& rgba)
    {
        (void)document;
        (void)item;
        (void)width;
        (void)height;
        (void)rgba;
        return false;
    }
    /// Runs one of Phoenix's own operations (the ones AI assistants use,
    /// such as in_betweens or lip_sync) on the active document, its
    /// arguments and result as JSON text; with an empty \a name, the list
    /// of them. False with \a error when it fails or there are none.
    virtual bool callOperation(const std::string& name, const std::string& argumentsJson, std::string& resultJson,
        std::string& error)
    {
        (void)name;
        (void)argumentsJson;
        (void)resultJson;
        error = "Phoenix's operations need the Phoenix window.";
        return false;
    }
    /// Imports the file at \a path (an image, sound, vector artwork or
    /// Photoshop document) into \a document's library and, unless
    /// \a toLibrary, onto the current frame, pushing its changes on the
    /// document's history. False with \a error when nothing came in.
    virtual bool importFile(fla::FLADocument* document, const std::string& path, bool toLibrary, std::string& error)
    {
        (void)document;
        (void)path;
        (void)toLibrary;
        error = "Importing files needs the Phoenix window.";
        return false;
    }
    /// The current frame (or every frame, numbered) of the document as PNG.
    virtual bool exportPNG(fla::FLADocument* document, const std::string& path, bool currentFrameOnly,
        std::string& error) = 0;
    /// The document's scene as an SWF movie.
    virtual bool exportSWF(fla::FLADocument* document, const std::string& path, std::string& error)
    {
        (void)document;
        (void)path;
        error = "Exporting SWF needs the stage, which this host does not have.";
        return false;
    }
    /// Publishes the document as its publish settings say, beside its file,
    /// naming the files written in \a written. False with \a error when
    /// something could not be written.
    virtual bool publish(fla::FLADocument* document, std::vector<std::string>& written, std::string& error)
    {
        (void)document;
        (void)written;
        error = "Publishing needs the stage, which this host does not have.";
        return false;
    }
};

} // namespace jsfl
