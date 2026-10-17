#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class Document;

namespace play {

/// A class's source as found: its text and the file it came from.
struct ClassFile
{
    std::string source;
    /// For messages: the file's name.
    std::string fileName;
};

/// Where a movie's ActionScript 3 classes are found, by qualified name
/// ("com.studio.Hat" in com/studio/Hat.as), in turn: the movie's folder,
/// the source paths its publish settings give, the sources of the
/// libraries they list (the src folder beside a .swc's lib folder), any
/// src folder in the movie's folder or the folders in it up to two deep,
/// the extra paths given, and last Phoenix's own classes.
class ClassPath
{
public:
    ClassPath() = default;
    /// The class path of \a document, saved in \a folder (UTF-8).
    ClassPath(const Document& document, const std::string& folder, const std::vector<std::string>& extra = {});

    /// The source of \a qualifiedName, or none.
    std::optional<ClassFile> find(const std::string& qualifiedName) const;

    /// The folders looked in, in order.
    const std::vector<std::filesystem::path>& roots() const { return _roots; }
    void addRoot(const std::filesystem::path& root);

    /// Phoenix's own classes: the source of a file by its path in the
    /// built-in folder ("fl/controls/Button.as"), or none.
    std::function<std::optional<std::string>(const std::string& relativePath)> builtIn;

private:
    std::vector<std::filesystem::path> _roots;
};

/// The ActionScript version a document's publish settings give (1, 2 or
/// 3), 0 when they say none.
int actionScriptVersion(const Document& document);

/// The document class a document's publish settings name, qualified;
/// empty for none.
std::string documentClassOf(const Document& document);

} // namespace play
} // namespace fla
