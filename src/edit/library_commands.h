#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Document;
class Folder;
class Symbol;
class SymbolInstance;

namespace library {

/// Every instance of \a symbol anywhere in the document: on the main
/// timelines, inside other symbols, and inside groups.
std::vector<SymbolInstance*> instancesOf(Document& document, const Symbol* symbol);

/// Whether \a name is free for a new library item or folder.
bool nameIsFree(const Document& document, const std::string& name);

/// \a base, or "base copy", "base copy 2"... whichever is free first, as
/// Animate names a duplicate.
std::string freeCopyName(const Document& document, const std::string& base);

/// A deep copy of \a symbol under \a name: its timelines, layers, frames
/// and everything on them. The caller owns it.
Symbol* cloneSymbol(const Symbol& symbol, const std::string& name);

} // namespace library

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

private:
    Document* _document;
    Symbol* _symbol;
    size_t _index = 0;
    bool _owned = false;
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
