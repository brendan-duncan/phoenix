#pragma once

#include "paint_brushes.h"

#include <string>
#include <vector>

namespace fla {

/// A brush in the brush library, which any document can draw with: its
/// definition, as a document keeps it, and the group it is filed under.
struct BrushLibraryEntry
{
    std::string category;
    PaintBrushDefinition definition;
    /// One of Phoenix's own brushes (phoenixBrushes), which come with it:
    /// not kept in the library's file, and not renamed or removed.
    bool builtIn = false;
};

/// The brush library's file, as writeBrushLibrary writes it: the brushes
/// a person has added, each with its category, in order.
std::vector<BrushLibraryEntry> readBrushLibrary(const std::string& xml);

/// \a entries (but Phoenix's own) as the brush library's file: a
/// <brushlibrary> of <paintbrushdefinition> elements as a document's
/// PaintBrushDefinitions.xml has them, each with its category.
std::string writeBrushLibrary(const std::vector<BrushLibraryEntry>& entries);

/// Adds \a definitions to \a library, filed under \a category or, when it
/// is empty, under paintBrushCategory's; a brush the library has already
/// (by guid) is left out. How many were added.
int addToBrushLibrary(std::vector<BrushLibraryEntry>& library, const std::vector<PaintBrushDefinition>& definitions,
    const std::string& category = std::string());

/// The entry of \a library with \a guid; null when none has it.
const BrushLibraryEntry* findInBrushLibrary(const std::vector<BrushLibraryEntry>& library, const std::string& guid);

/// Phoenix's own brushes: art and pattern brushes drawn for Phoenix (a
/// tapered stroke, a ribbon, an arrow, dash-dot, a vine, a rope; dots,
/// chain, stitches, scallops, zigzag), so the library has brushes to draw
/// with before any are added. Filed under "Phoenix".
const std::vector<BrushLibraryEntry>& phoenixBrushes();

} // namespace fla
