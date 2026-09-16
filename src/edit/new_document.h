#pragma once

namespace fla {

class FLADocument;

/// The stage a new document starts with, matching Animate's own default.
constexpr int kDefaultDocumentWidth = 550;
constexpr int kDefaultDocumentHeight = 400;
constexpr double kDefaultDocumentFrameRate = 24.0;

/// Builds an empty document: one scene, one layer, one empty keyframe.
///
/// The result is shaped like a parsed one rather than a minimal stand-in, so
/// everything downstream -- the view, the timeline, the writer -- sees what it
/// would see from a file. In particular the layer's frame range is filled in,
/// which is bookkeeping the parser does and which frame resolution relies on.
///
/// The caller owns the result, and deleting it deletes the whole tree.
FLADocument* createEmptyDocument(int width = kDefaultDocumentWidth,
    int height = kDefaultDocumentHeight,
    double frameRate = kDefaultDocumentFrameRate);

} // namespace fla
