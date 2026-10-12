#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;

/// How two versions of a document differ part by part: the scenes (the
/// document's own file), each symbol, each bitmap and sound, compared as
/// they would be written, so only real changes show.
struct DocumentChange
{
    enum class Kind
    {
        Added,
        Removed,
        Changed
    };
    Kind kind = Kind::Changed;
    /// "Scenes", "Symbol: Ball", "Media: bin/M 1.dat"...
    std::string item;
    /// For a changed text part: how many of its lines differ.
    int linesChanged = 0;
};

/// The changes from \a before to \a after. Empty when they are the same.
std::vector<DocumentChange> compareDocuments(const Document& before, const Document& after, std::string* error = nullptr);

/// A short label for \a kind: "added", "removed", "changed".
const char* changeName(DocumentChange::Kind kind);

} // namespace fla
