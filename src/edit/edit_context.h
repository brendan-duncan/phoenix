#pragma once

#include "command_stack.h"

namespace fla {

class FLADocument;

/// The editable state of the open document: the document itself, its undo
/// history, and whether it has unsaved changes.
///
/// This is the seam between the read-only rendering path and the editing tools.
/// The views keep taking const pointers for drawing, since drawing does not
/// mutate; tools reach the document through here instead, which keeps the
/// distinction visible rather than de-consting the whole render path.
///
/// Free of Qt, so the editing core stays independent of the GUI layer.
class EditContext
{
public:
    EditContext() = default;

    EditContext(const EditContext&) = delete;
    EditContext& operator=(const EditContext&) = delete;

    /// Points the context at a newly loaded document and drops the undo
    /// history, which refers to the previous document's objects.
    ///
    /// Does not take ownership: the caller still owns the document and must keep
    /// it alive for as long as it is set here.
    void setDocument(FLADocument* document);

    FLADocument* document() const { return _document; }

    bool hasDocument() const { return _document != nullptr; }

    CommandStack& commandStack() { return _commandStack; }

    const CommandStack& commandStack() const { return _commandStack; }

    /// Whether the document has changes that have not been saved. A context with
    /// no document is never modified.
    bool isModified() const { return _document != nullptr && !_commandStack.isClean(); }

    /// Records that the document has just been written to disk, so the current
    /// history position counts as saved.
    void markSaved() { _commandStack.setClean(); }

private:
    /// Borrowed, not owned.
    FLADocument* _document = nullptr;

    CommandStack _commandStack;
};

} // namespace fla
