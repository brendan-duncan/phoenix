#pragma once

#include "command.h"

#include "../data/lazy_bytes.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;

/// The reference board: pictures pinned on and around the stage to draw
/// from (a model sheet, a color key, a photo). They belong to the document,
/// the same in every scene, and are only ever shown in the view: they are
/// not art, not in the timeline or the library, and never drawn into an
/// export, a playback or a published file.
///
/// The images' files ride in the document's package (bin/phoenix_reference_
/// ...), which other apps that open it ignore, and where each one sits is
/// one Phoenix attribute on the document. Free of Qt.
namespace references {

struct Image
{
    /// The file in the package that holds the picture.
    std::string path;
    /// What it was called when it was added, to tell it apart.
    std::string name;
    /// Its middle, in the stage's coordinates (the scene's, whatever symbol
    /// is being edited).
    double x = 0.0;
    double y = 0.0;
    /// Its size against the picture's own pixels.
    double scale = 1.0;
    /// Degrees clockwise.
    double rotation = 0.0;
    /// 0 to 1.
    double opacity = 1.0;
    bool locked = false;
    bool hidden = false;

    bool operator==(const Image& other) const;
    bool operator!=(const Image& other) const { return !(*this == other); }
};

struct Board
{
    /// Bottom to top.
    std::vector<Image> images;
    /// Every image hidden at once, each keeping its own setting.
    bool hidden = false;

    bool operator==(const Board& other) const;
    bool operator!=(const Board& other) const { return !(*this == other); }
};

/// The attribute on the document that holds the board.
extern const char* const kAttribute;
/// Where the pictures' files go in the package.
extern const char* const kFilePrefix;

std::string toText(const Board& board);
Board fromText(const std::string& text);

/// The document's board; empty when it has none.
Board of(const Document& document);

using Attributes = std::vector<std::pair<std::string, std::string>>;
using Files = std::vector<std::pair<std::string, LazyBytes>>;

/// The document's attributes with \a board in them, the attribute left out
/// when the board is empty, so a document that never used one stays as it
/// was.
Attributes withBoard(const Document& document, const Board& board);
/// The document's package files with only the pictures \a board still uses
/// (and every file that is not a picture of the board's).
Files filesFor(const Document& document, const Board& board);

/// A package path for a new picture, unused in \a document and among
/// \a taken, ending in \a extension ("png", "jpg").
std::string newPath(const Document& document, const std::string& extension, const Files& taken = {});

/// The bytes of the picture at \a path; empty when there is none.
const std::vector<uint8_t>& bytesOf(const Document& document, const std::string& path);

/// Puts a new board in the document, with the files it needs, undoably:
/// the attribute and the package files change together.
class SetBoardCommand : public Command
{
public:
    /// \a added are pictures the new board uses that are not in the
    /// package yet.
    SetBoardCommand(Document& document, const Board& board, const Files& added, const std::string& name,
        int mergeId = -1);

    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }
    int mergeId() const override { return _mergeId; }
    bool mergeWith(const Command* other) override;

private:
    Document* _document;
    Attributes _beforeAttributes;
    Files _beforeFiles;
    Attributes _afterAttributes;
    Files _afterFiles;
    std::string _name;
    int _mergeId;
};

} // namespace references

} // namespace fla
