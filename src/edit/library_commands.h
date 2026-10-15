#pragma once

#include "command.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Bitmap;
class Document;
class Element;
class Folder;
class Symbol;
class SymbolInstance;

namespace library {

/// Every instance of \a symbol anywhere in the document: on the main
/// timelines, inside other symbols, and inside groups.
std::vector<SymbolInstance*> instancesOf(Document& document, const Symbol* symbol);

/// How many times each library item is used, by name: the instances of a
/// symbol or a bitmap, the shapes filling with a bitmap and the frames
/// playing a sound, anywhere in the document, inside symbols too. Items
/// not used have no entry.
std::map<std::string, int> useCounts(Document& document);

/// The library items the document needs: those its scenes use, those
/// exported for ActionScript, and whatever those use in turn. The rest is
/// what Select Unused Items picks; a symbol used only by unused symbols is
/// unused too.
std::set<std::string> usedItems(Document& document);

/// A symbol's linkage: the class it is exported for ActionScript as, or
/// its identifier, or empty when it is not exported.
std::string linkageOf(const Symbol& symbol);

/// \a attributes with the linkage class set to \a className and export
/// for ActionScript on, or the linkage taken off when it is empty.
std::vector<std::pair<std::string, std::string>> withLinkage(
    std::vector<std::pair<std::string, std::string>> attributes, const std::string& className);

/// Whether \a name is free for a new library item or folder.
bool nameIsFree(const Document& document, const std::string& name);

/// \a base, or "base copy", "base copy 2"... whichever is free first: the
/// name a duplicate gets.
std::string freeCopyName(const Document& document, const std::string& base);

/// A deep copy of \a symbol under \a name: its timelines, layers, frames
/// and everything on them. The caller owns it.
Symbol* cloneSymbol(const Symbol& symbol, const std::string& name);

/// Library names are paths: "Characters/Head/eye" is the item "eye" in the
/// folder "Head" in the folder "Characters". Renaming an item or a folder,
/// or moving it into another folder, is a change of path; these are the
/// changes, each from a full name to a full name.
using Renames = std::vector<std::pair<std::string, std::string>>;

/// Whether \a name is one of the library's folders.
bool isFolder(const Document& document, const std::string& name);

/// The changes that rename or move \a from, an item or a folder, to the
/// full name \a to. A folder takes its folders and items along. Empty, with
/// \a problem saying why, when \a from names nothing, \a to is taken, the
/// folder \a to would be in does not exist, or a folder would go inside
/// itself.
Renames pathRenames(const Document& document, const std::string& from, const std::string& to,
    std::string* problem = nullptr);

/// Makes the changes: the folders, symbols, bitmaps and sounds named, and
/// everything that refers to them by name: symbol and bitmap instances,
/// bitmap fills, and the frames that play a sound.
void applyRenames(Document& document, const Renames& renames);

} // namespace library

/// Renames or moves library items and folders, as one undoable step.
class RenameLibraryCommand : public Command
{
public:
    RenameLibraryCommand(Document* document, library::Renames renames, const std::string& name);
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    Document* _document;
    library::Renames _renames;
    library::Renames _inverse;
    std::string _name;
};

/// Renames a symbol, and every instance of it with it.
class RenameSymbolCommand : public Command
{
public:
    RenameSymbolCommand(Document* document, Symbol* symbol, const std::string& name);
    void redo() override;
    void undo() override;
    std::string name() const override { return "Rename Symbol"; }

private:
    void apply(const std::string& from, const std::string& to);

    Document* _document;
    Symbol* _symbol;
    std::string _before;
    std::string _after;
};

/// Adds a symbol to the library, as Duplicate does.
class AddSymbolCommand : public Command
{
public:
    /// Takes ownership of \a symbol.
    AddSymbolCommand(Document* document, Symbol* symbol, const std::string& name);
    ~AddSymbolCommand() override;
    AddSymbolCommand(const AddSymbolCommand&) = delete;
    AddSymbolCommand& operator=(const AddSymbolCommand&) = delete;
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    Document* _document;
    Symbol* _symbol;
    std::string _name;
    bool _owned = true;
};

/// Takes a symbol out of the library. Only for a symbol with no instances:
/// the caller checks.
class RemoveSymbolCommand : public Command
{
public:
    RemoveSymbolCommand(Document* document, Symbol* symbol);
    ~RemoveSymbolCommand() override;
    RemoveSymbolCommand(const RemoveSymbolCommand&) = delete;
    RemoveSymbolCommand& operator=(const RemoveSymbolCommand&) = delete;
    void redo() override;
    void undo() override;
    std::string name() const override { return "Delete Symbol"; }
    Symbol* symbol() const { return _symbol; }

private:
    Document* _document;
    Symbol* _symbol;
    size_t _index = 0;
    bool _owned = false;
};

/// Deletes a symbol along with every instance of it, wherever they are:
/// on the scenes, inside other symbols and inside groups.
class DeleteSymbolCommand : public Command
{
public:
    DeleteSymbolCommand(Document* document, Symbol* symbol);
    ~DeleteSymbolCommand() override;
    DeleteSymbolCommand(const DeleteSymbolCommand&) = delete;
    DeleteSymbolCommand& operator=(const DeleteSymbolCommand&) = delete;
    void redo() override;
    void undo() override;
    std::string name() const override { return "Delete Symbol"; }

private:
    struct Removed
    {
        std::vector<Element*>* list;
        size_t index;
        Element* element;
    };

    Document* _document;
    RemoveSymbolCommand _remove;
    std::vector<Removed> _removed;
};

/// Takes a bitmap out of the library. Only for a bitmap nothing uses: the
/// caller checks.
class RemoveBitmapCommand : public Command
{
public:
    RemoveBitmapCommand(Document* document, Bitmap* bitmap);
    /// A bitmap still out of the library when the history lets go of this
    /// step is the command's alone, and goes with it.
    ~RemoveBitmapCommand() override;
    void redo() override;
    void undo() override;
    std::string name() const override { return "Delete Bitmap"; }

private:
    Document* _document;
    Bitmap* _bitmap;
    size_t _index = 0;
    bool _removed = false;
};

/// Adds an empty library folder.
class AddFolderCommand : public Command
{
public:
    AddFolderCommand(Document* document, const std::string& name);
    ~AddFolderCommand() override;
    AddFolderCommand(const AddFolderCommand&) = delete;
    AddFolderCommand& operator=(const AddFolderCommand&) = delete;
    void redo() override;
    void undo() override;
    std::string name() const override { return "New Folder"; }

private:
    Document* _document;
    Folder* _folder;
    bool _owned = true;
};

} // namespace fla
