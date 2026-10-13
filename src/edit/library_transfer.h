#pragma once

#include "command.h"

#include <map>
#include <string>
#include <vector>

namespace fla {

class Document;
class Element;
class Frame;

/// Library items brought from one document into another: symbols with the
/// symbols, bitmaps and sounds they use, so a character arrives whole, as
/// copying between libraries, dragging from an external library or
/// pasting from another document does. Free of Qt.
namespace transfer {

/// What to do with an item the target already has one of by name, as
/// Resolve Library Conflicts asks.
enum class Resolution
{
    /// The target's item takes the copied one's contents; what uses it
    /// shows the new one.
    Replace,
    /// The copy comes in under a free name ("Head copy"), and what was
    /// copied with it uses that name.
    KeepBoth,
    /// The target's item stays as it is, and what was copied uses it.
    Skip
};

/// \a items (library names: symbols, bitmaps, sounds) and everything they
/// use, in \a source.
std::vector<std::string> withDependencies(const Document& source, const std::vector<std::string>& items);

/// Which of \a items and what they use the target already has by name.
std::vector<std::string> conflicts(const Document& target, const Document& source, const std::vector<std::string>& items);

/// Copies \a items and what they use into \a target as one undo step; null
/// when there is nothing to copy. \a added, when given, gets the names the
/// items have in the target; \a renamedOut the name in the target of each
/// item and of everything it uses.
CommandPtr copyItems(Document& target, const Document& source, const std::vector<std::string>& items,
    Resolution resolution, std::vector<std::string>* added = nullptr,
    std::map<std::string, std::string>* renamedOut = nullptr);

/// The library items \a elements use themselves: symbols and bitmaps placed,
/// bitmaps filling shapes, inside groups too. What those symbols use in
/// turn is withDependencies' to find.
std::vector<std::string> usedBy(const std::vector<const Element*>& elements);
/// The same for \a frames, with the sounds they play.
std::vector<std::string> usedByFrames(const std::vector<const Frame*>& frames);

/// Points what \a element (or \a frame) uses at new library names, by
/// \a names (from copyItems: old name to the name in the target).
void renameUses(Element& element, const std::map<std::string, std::string>& names);
void renameUses(Frame& frame, const std::map<std::string, std::string>& names);

} // namespace transfer

} // namespace fla
