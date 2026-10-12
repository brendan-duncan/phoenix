#pragma once

#include "command.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Document;

/// The Swatches panel's colors: the document's own swatch list, which
/// Animate saves in the file, and Animate's default palette for a document
/// that has none.
namespace swatches {

using Color = std::array<uint8_t, 4>;

/// Animate's default palette: a ramp of grays and the primaries, then the
/// 216 web-safe colors.
std::vector<Color> defaults();

/// The solid colors of the document's first swatch list; empty when it has
/// none.
std::vector<Color> of(const Document& document);

/// The document's first swatch list as text, and whether it has one.
struct State
{
    bool exists = false;
    std::string xml;
    std::vector<Color> colors;
};
State stateOf(const Document& document);

/// \a state with \a color added at the end; with no list yet, a new one
/// named Swatches, as Animate makes it.
State withAdded(const State& state, const Color& color);

/// \a state without its solid swatch \a index. Swatches it does not model
/// (gradients) stay as they were.
State withRemoved(const State& state, size_t index);

} // namespace swatches

/// Puts the document's swatch list in one state or the other.
class SetSwatchesCommand : public Command
{
public:
    SetSwatchesCommand(Document* document, swatches::State before, swatches::State after, const std::string& name);
    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

private:
    void apply(const swatches::State& state);

    Document* _document;
    swatches::State _before;
    swatches::State _after;
    std::string _name;
};

} // namespace fla
