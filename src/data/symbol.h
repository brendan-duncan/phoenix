#pragma once

#include "dom_element.h"
#include "resource.h"
#include "timeline.h"
#include <utility>
#include <vector>
#include <string>

namespace fla {

class Symbol : public Resource
{
public:
    std::string name;
    std::string itemId;
    std::string lastModified;
    /// Path of this symbol's file inside LIBRARY/, as the document's Include
    /// referred to it. Usually the name plus ".xml", but not necessarily, so it
    /// is kept rather than derived.
    std::string href;

    /// How new instances behave by default: the symbol's own type as the
    /// library shows it.
    SymbolType symbolType = SymbolType::Graphic;

    /// A symbol type Phoenix does not model ("ik container" for a shape
    /// with bones, "puppet" for Asset Warp), kept as spelled; symbolType is
    /// Graphic for it.
    std::string otherSymbolType;

    /// The attributes of the Include that names the symbol in the document
    /// (itemIcon, loadImmediate, itemID, lastModified), as read.
    std::vector<std::pair<std::string, std::string>> includeAttributes;

    /// 9-slice scaling guides, in the symbol's own space: the two vertical
    /// and two horizontal lines that split it into corners that never
    /// scale, edges that scale one way and a middle that scales both.
    /// Present only when the symbol has them switched on.
    bool scale9 = false;
    double scaleGridLeft = 0.0;
    double scaleGridTop = 0.0;
    double scaleGridRight = 0.0;
    double scaleGridBottom = 0.0;

    std::vector<Timeline*> timelines;

    Symbol(DOMElement* parent)
        : Resource(parent)
    {}

    ~Symbol() override;

    Type resourceType() const override { return Type::Symbol; }

    std::string domTypeName() const override { return "Symbol"; }

    DOMType domType() const override { return DOMType::Symbol; }

    static DOMType staticDomType() { return DOMType::Symbol; }
};

class Document;

/// Whether \a outer shows \a target anywhere inside it, at any depth: on
/// its timelines, inside groups, and inside the symbols it places. A symbol
/// counts as containing itself. Placing \a target inside \a outer would make
/// a symbol contain itself, which nothing can draw, when
/// symbolContains(target, outer); every way of placing a symbol checks this.
bool symbolContains(const Symbol* outer, const Symbol* target);

class Element;

/// Whether placing \a element inside \a outer would make a symbol contain
/// itself: \a element is, or holds in a group, an instance of a symbol that
/// contains \a outer. Always false when \a outer is null (a scene).
bool placingNestsSymbol(const Element& element, const Symbol* outer);

/// Cuts every loop of symbols containing themselves in a document just read
/// (A inside A, or A inside B inside A): the instance that closes a loop
/// keeps its library name, so the file saves as it was, but no longer links
/// to its symbol, so it is drawn as a missing symbol. Returns how many
/// instances were cut.
int breakSymbolCycles(Document& document);

} // namespace fla
