#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fla {

class DOMElement;
class Element;
class Shape;

/// Soft edges on vector art: a fill or a stroke whose edge fades out over
/// a distance rather than ending sharp, for soft shading, glows and blurred
/// shapes that stay vectors (no blur filter, no picture). The fade is
/// centered on the edge, half inside and half outside, as a blur of that
/// many pixels would leave it. Each fill and stroke style keeps its own in
/// the phoenixFeather attribute, which other apps ignore. Free of Qt.
namespace feather {

extern const char* const kAttribute;

/// The feather of a fill or stroke style, in the shape's own pixels; 0 for
/// a hard edge.
double of(const DOMElement& style);
/// \a style's attributes with its feather set to \a amount (taken out at
/// 0).
std::vector<std::pair<std::string, std::string>> with(const DOMElement& style, double amount);
/// Sets \a style's feather to \a amount.
void set(DOMElement& style, double amount);

/// Whether any fill or stroke of \a shape is feathered.
bool any(const Shape& shape);
/// Whether \a element (a shape, a primitive, or a group with one in it) is
/// feathered anywhere.
bool any(const Element& element);

/// The feather of \a shape's first fill, or of its first stroke (0 with
/// none); \a mixed says whether the others differ from it.
double ofFills(const Shape& shape, bool* mixed = nullptr);
double ofStrokes(const Shape& shape, bool* mixed = nullptr);
/// Every fill (stroke) of \a shape feathered by \a amount.
void setFills(Shape& shape, double amount);
void setStrokes(Shape& shape, double amount);

} // namespace feather

} // namespace fla
