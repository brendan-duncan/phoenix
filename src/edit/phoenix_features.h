#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;

/// The features of Phoenix's own that an FLA has no place for. Phoenix keeps
/// each in the file all the same, as attributes of its own (named phoenix...)
/// that other FLA editors leave alone and Phoenix reads back, so FLA and XFL
/// files keep them. What another editor makes of the file differs: it shows
/// the artwork without them, and drops them if it saves the file again.
///
/// This is the one list of them, for the warning before an FLA or XFL save,
/// for `Phoenix --convert`, and for the documentation.
namespace features {

struct Feature
{
    /// A short name for the file and the command line: "textured-brushes".
    const char* id;
    /// What a person calls it: "Textured brushes".
    const char* title;
    /// How another FLA editor shows a document that uses it.
    const char* elsewhere;
    /// The attributes that mark its use; one present means the feature is.
    std::vector<const char*> attributes;
};

/// Every Phoenix-only feature, in the order the warning lists them.
const std::vector<Feature>& all();

/// A feature a document uses: which, how many times, and the first place.
struct Use
{
    const Feature* feature = nullptr;
    int count = 0;
    std::string where;
};

/// The Phoenix-only features \a document uses, in all()'s order. Any other
/// phoenix... attribute counts towards "other Phoenix settings".
std::vector<Use> used(const Document& document);

/// The ids of what used() finds, for Document.features in a .phx.
std::vector<std::string> ids(const std::vector<Use>& uses);

} // namespace features

} // namespace fla
