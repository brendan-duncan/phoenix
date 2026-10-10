#pragma once

namespace fla {

class ColorTransform;
class DOMElement;
class Element;
class StaticText;

/// Deep-copies any element that can sit in a frame: shapes with their edges
/// and styles, groups with their members, symbol and bitmap instances with
/// the library item they point at, text with its runs, and primitives with
/// their styles. The copy owns everything it holds and has no parent beyond
/// \a parent. Returns null for an element kind that cannot be copied.
Element* cloneElement(const Element& element, DOMElement* parent);

/// Replaces a text's box and runs with another's, leaving its transform and
/// its place in the frame alone. What the text tool's undo swaps.
void setTextContents(StaticText& target, const StaticText& source);

/// Bakes a color effect into an element's own colors: every solid fill,
/// gradient stop and stroke color of its shapes and primitives, down
/// through groups. What Break Apart does with an instance's color effect,
/// since the copies no longer have an instance to carry it.
void applyColorTransform(Element& element, const ColorTransform& transform);


} // namespace fla
