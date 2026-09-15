#include "test_util.h"

#include "../src/edit/edit_context.h"

#include <string>

using fla::Command;
using fla::CommandPtr;
using fla::EditContext;

namespace {

/// Minimal command that flips a flag, enough to move the context off its clean
/// state without dragging the whole document model into this test.
class FlagCommand : public Command
{
public:
    explicit FlagCommand(bool* flag)
        : _flag(flag)
    {}

    void redo() override { *_flag = true; }

    void undo() override { *_flag = false; }

    std::string name() const override { return "Set Flag"; }

private:
    bool* _flag;
};

/// The context only stores the pointer, so tests never dereference it and a
/// stand-in address is enough.
fla::FLADocument* fakeDocument(int id)
{
    return reinterpret_cast<fla::FLADocument*>(static_cast<intptr_t>(0x1000 + id));
}

} // namespace

TEST(context_starts_empty)
{
    EditContext context;

    CHECK(!context.hasDocument());
    CHECK(context.document() == nullptr);
    CHECK(!context.isModified());
}

TEST(context_without_a_document_is_never_modified)
{
    EditContext context;
    bool flag = false;

    // Even with history present, no document means nothing to save.
    context.commandStack().push(CommandPtr(new FlagCommand(&flag)));
    CHECK(flag);
    CHECK(!context.isModified());
}

TEST(context_tracks_modification)
{
    EditContext context;
    bool flag = false;
    context.setDocument(fakeDocument(1));

    CHECK(context.hasDocument());
    CHECK(!context.isModified()); // a freshly loaded document is unmodified

    context.commandStack().push(CommandPtr(new FlagCommand(&flag)));
    CHECK(context.isModified());

    context.markSaved();
    CHECK(!context.isModified());

    // Undoing away from the saved position modifies it again.
    context.commandStack().undo();
    CHECK(context.isModified());

    context.commandStack().redo();
    CHECK(!context.isModified());
}

TEST(context_set_document_clears_history)
{
    EditContext context;
    bool flag = false;
    context.setDocument(fakeDocument(1));
    context.commandStack().push(CommandPtr(new FlagCommand(&flag)));
    CHECK(context.commandStack().canUndo());
    CHECK(context.isModified());

    // The old history points into the document being replaced, so it must go.
    context.setDocument(fakeDocument(2));

    CHECK(context.document() == fakeDocument(2));
    CHECK(!context.commandStack().canUndo());
    CHECK(!context.commandStack().canRedo());
    CHECK(context.commandStack().count() == 0);
    CHECK(!context.isModified());
}

TEST(context_closing_a_document_clears_history)
{
    EditContext context;
    bool flag = false;
    context.setDocument(fakeDocument(1));
    context.commandStack().push(CommandPtr(new FlagCommand(&flag)));

    context.setDocument(nullptr);

    CHECK(!context.hasDocument());
    CHECK(!context.isModified());
    CHECK(context.commandStack().count() == 0);
}
