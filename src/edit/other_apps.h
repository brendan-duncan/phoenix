#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;

/// Phoenix's own compositing and templates made into what any app that opens
/// FLA files shows, for File > Export > Export FLA for Other Apps. Free of
/// Qt. It changes the document it is given for good: it is for a copy being
/// written, never the document being edited.
namespace otherapps {

/// Whether \a document has anything bakeCompositing would change.
bool needsBaking(const Document& document);

/// - Template values: each instance shows a copy of its symbol with its
///   words and colors written in (one copy for each different set of
///   values, nested symbols too).
/// - Track mattes: an alpha matte becomes a mask over the layer, its shape
///   hard-edged; a luma matte the same, by shape rather than light.
///   Inverted mattes cannot be carried and are left drawn plainly.
/// - Adjustment layers: the layers below go into a movie clip of their own,
///   placed on the adjustment layer with the effects as its filters (Blur,
///   Adjust Color) and color effect (Tint), keyed a frame at a time where
///   the settings change. The movie clip plays on its own in other apps.
///
/// \a notes says what could only be carried in part.
void bakeCompositing(Document& document, std::vector<std::string>& notes);

} // namespace otherapps

} // namespace fla
