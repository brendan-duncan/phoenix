#pragma once

#include "../data/filter.h"

#include <QImage>
#include <QPainter>

#include <string>
#include <vector>

/// Flash's filters and blend modes, applied to an instance that has been
/// rendered into an image on its own.
namespace Filters {

/// How far, in device pixels, the filters can reach beyond the instance's
/// own pixels, so the offscreen can be made that much larger all round.
/// \a scale is device pixels per stage pixel at the instance.
double margin(const std::vector<fla::Filter>& filters, double scale);

/// The filters applied in order to \a image, which must be premultiplied
/// ARGB and already carry the margin. The result is the same size.
QImage apply(const QImage& image, const std::vector<fla::Filter>& filters, double scale);

/// The composition mode for an instance's blendMode. "normal" and "layer"
/// are plain source-over (a layer is isolated by being rendered on its
/// own). Modes Qt cannot composite (subtract, invert, alpha, erase) come
/// back as source-over with \a supported false.
QPainter::CompositionMode blendMode(const std::string& name, bool& supported);

/// Whether an instance needs rendering on its own before it is composited.
bool needsIsolation(const std::string& blendModeName);

/// Draws  image at  at with the instance's blend mode. Subtract and
/// invert, which Qt has no mode for, are composed from the ones it has:
/// invert is the backdrop differenced with white under the instance's
/// coverage, and subtract inverts, adds and inverts again. Alpha and erase
/// cut into what is under them, which only means something inside a parent
/// with the Layer blend mode ( insideLayerGroup): alpha keeps what is
/// under its coverage, erase removes it. Elsewhere Flash shows nothing of
/// them, so nothing is drawn.
void composite(QPainter& painter, const QPoint& at, const QImage& image, const std::string& blendModeName,
    bool insideLayerGroup = false);

} // namespace Filters
