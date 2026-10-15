#pragma once

#include <string>

namespace fla {

class Layer;
class Symbol;
class SymbolList;
class Timeline;

/// Pegs: a layer that holds nothing to see, only a transform, which the
/// layers parented to it follow. Animating the peg moves, turns and scales
/// everything under it about its pivot. Built on the FLA's layer
/// parenting: the peg layer holds one instance of an empty graphic symbol,
/// so other apps that open the file play it the same and simply draw
/// nothing for the peg. Free of Qt.
namespace pegs {

/// Whether \a symbol is a peg's (marked phoenixPeg).
bool isPeg(const Symbol& symbol);

/// The library's peg symbol, or null when it has none.
Symbol* pegSymbol(const SymbolList& library);

/// A new peg symbol named \a name: one empty layer, marked. The caller owns
/// it until it is added to a library.
Symbol* newPegSymbol(const std::string& name);

/// A new peg layer for \a timeline, named "Peg N" with the next free
/// number, holding one keyframe as long as the timeline with an instance
/// of \a peg at \a x, \a y. The caller owns it until it is added.
Layer* newPegLayer(const Timeline& timeline, Symbol* peg, double x, double y);

} // namespace pegs

} // namespace fla
