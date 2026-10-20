#pragma once

#include "host.h"
#include "object.h"

#include "../edit/command.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace fla {
class DOMElement;
class Document;
class FLADocument;
class Layer;
class Timeline;
} // namespace fla

namespace jsfl {

namespace xul {
class Dialog;
}

/// One script run: the host it works through, the undo steps it opened, and
/// what it keeps between calls (its frame and library selections).
///
/// Every change a run makes goes through push(), which gathers a document's
/// changes into one undo step named after the script. A history cannot be
/// moved while a step is open, so opening, closing or switching documents
/// closes the steps first (beforeSwitch); changes after that start another.
///
/// The model's objects hold plain pointers into the documents. Before use,
/// each is checked against what the document holds now (alive()): a layer
/// deleted, an element undone or a document closed gives a clear error
/// rather than a crash.
class Session
{
public:
    Session(Host& host, std::string undoName);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Host& host() const { return _host; }
    const std::string& undoName() const { return _undoName; }

    /// Closes the undo steps the run opened and brings the view up to date.
    void finish();
    /// For a script that stays loaded (a tool): closes the steps one call
    /// opened, forgets what it knew of the documents (they change between
    /// calls) and brings the view up to date; the next change starts a new
    /// step.
    void settle();
    void setUndoName(const std::string& name) { _undoName = name; }
    /// The host is going away (the window closing with a tool loaded):
    /// nothing more is asked of it.
    void detach() { _finished = true; }

    // Documents.
    bool isOpen(const fla::FLADocument* document) const;
    /// The document's data; an error when it was closed.
    fla::Document& data(fla::FLADocument* document) const;
    /// Before a host call that may put documents aside or close them.
    void beforeSwitch();
    /// After closing a document: what the run kept about it goes.
    void forget(const fla::FLADocument* document);

    // Changes.
    /// An error when the document must not change.
    void checkWritable(fla::FLADocument* document);
    /// Opens the run's undo step on the document, for a host call that
    /// pushes its own commands (an import); push() opens it too.
    void beginStep(fla::FLADocument* document);
    /// Applies \a command as part of the run's undo step. \a structural
    /// changes add or take away objects, which liveness checks must see.
    void push(fla::FLADocument* document, fla::CommandPtr command, bool structural = true);
    /// Whether \a object is in the document now.
    bool alive(fla::FLADocument* document, const fla::DOMElement* object);
    /// Something outside push() changed what the document holds.
    void invalidate(fla::FLADocument* document);

    // What the run keeps.
    std::vector<std::string>& librarySelection(fla::FLADocument* document);
    std::vector<FrameRange>& frameSelection(fla::FLADocument* document, const fla::Timeline* timeline);
    std::vector<int>& layerSelection(fla::FLADocument* document, const fla::Timeline* timeline);
    /// The playhead and current layer of a timeline other than the one
    /// being edited (whose own come from the host).
    struct Position
    {
        int frame = 0;
        int layer = 0;
    };
    Position& position(const fla::Timeline* timeline) { return _positions[timeline]; }

    /// The script's own file, as a platform path (empty for a string).
    std::string scriptPath;

    /// Runs code from an XML dialog (its handlers, its SWF panels) in the
    /// script's context; the error it threw, or empty, and its value as
    /// text in \a value when given. Set by the engine.
    std::function<std::string(const std::string& code, std::string* value)> evaluate;
    /// The XML dialogs open, innermost last (fl.xmlui works on that one).
    std::vector<xul::Dialog*> dialogs;

private:
    struct DocumentState
    {
        bool stepOpen = false;
        bool reachableValid = false;
        std::unordered_set<const fla::DOMElement*> reachable;
        std::vector<std::string> librarySelection;
        std::map<const fla::Timeline*, std::vector<FrameRange>> frames;
        std::map<const fla::Timeline*, std::vector<int>> layers;
    };
    DocumentState& state(fla::FLADocument* document);
    void closeSteps();
    /// The frames the run selected, shown in the timeline.
    void showFrameSelections();

    Host& _host;
    std::string _undoName;
    std::map<const fla::FLADocument*, DocumentState> _documents;
    std::map<const fla::Timeline*, Position> _positions;
    bool _finished = false;
};

} // namespace jsfl
