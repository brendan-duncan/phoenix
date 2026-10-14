#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Element;
class Frame;

/// The scripts the Actions panel edits: a keyframe's, and the ActionScript 1
/// and 2 handlers kept on a button or movie clip instance (its on() and
/// onClipEvent() blocks). Free of Qt.
namespace scripts {

/// The script on \a element (a button's or a movie clip's), or empty.
std::string instanceScript(const Element& element);

/// \a extraChildren with the instance script set to \a code: the Actionscript
/// child replaced, added, or taken out when \a code is empty. The rest stay
/// as they were.
std::vector<std::string> withInstanceScript(const std::vector<std::string>& extraChildren, const std::string& code);

/// The merge id of script edits: what is typed into one script, one pause
/// after another, is one step to undo.
constexpr int kTypingMergeId = 200;

/// Sets a keyframe's script, taking it off when \a code is empty. Commands
/// with the same \a session (not negative) on the same frame merge.
class SetFrameScriptCommand : public Command
{
public:
    SetFrameScriptCommand(Frame* frame, const std::string& code, int session = -1);

    void redo() override;
    void undo() override;
    std::string name() const override { return "Edit Actions"; }
    int mergeId() const override { return _session >= 0 ? kTypingMergeId : -1; }
    bool mergeWith(const Command* other) override;

private:
    void apply(const std::string& code);

    Frame* _frame;
    std::string _before;
    std::string _after;
    int _session;
};

/// Sets the script on a button or movie clip instance, taking it off when
/// \a code is empty. Commands with the same \a session on the same instance
/// merge.
class SetInstanceScriptCommand : public Command
{
public:
    SetInstanceScriptCommand(Element* element, const std::string& code, int session = -1);

    void redo() override;
    void undo() override;
    std::string name() const override { return "Edit Actions"; }
    int mergeId() const override { return _session >= 0 ? kTypingMergeId : -1; }
    bool mergeWith(const Command* other) override;

private:
    Element* _element;
    std::vector<std::string> _before;
    std::vector<std::string> _after;
    int _session;
};

} // namespace scripts

} // namespace fla
