#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;
class Symbol;

/// Linked shared assets: a symbol in a shot file kept in step with the
/// same symbol in a master file (a character rig, a prop), as shared
/// library items are. The link is the FLA's own: the symbol's
/// sourceFlashFilepath and sourceLibraryItemHRef, so other apps that
/// update shared items work on the same files. What is written is still an ordinary embedded
/// symbol. Free of Qt.
namespace links {

struct Link
{
    /// The master file, as written: relative to the shot file, or absolute.
    std::string master;
    /// The symbol's name in the master's library.
    std::string item;
    bool autoUpdate = false;
};

/// The link \a symbol carries, if any.
bool linkOf(const Symbol& symbol, Link& link);
void setLink(Symbol& symbol, const Link& link);
void clearLink(Symbol& symbol);

/// The names of the symbols in \a document that are linked.
std::vector<std::string> linkedSymbols(const Document& document);

/// Brings the symbol \a name of \a target up to date with \a item of
/// \a master: its timeline is replaced with a copy of the master's, and
/// what that uses (symbols, bitmaps) is copied over or replaced too, so a
/// rig arrives whole. The symbol keeps its link and its place in the
/// library, and instances of it anywhere stay instances of it. Returns
/// the names of the library items that changed; empty with \a error set
/// when the master has no such item.
std::vector<std::string> update(Document& target, const std::string& name, const Document& master,
    const std::string& item, std::string& error);

/// The master's path for a link, resolved against the shot file's folder.
std::string resolve(const std::string& master, const std::string& shotFile);

} // namespace links

} // namespace fla
