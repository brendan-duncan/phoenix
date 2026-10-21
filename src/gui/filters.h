#pragma once

#include "../data/filter.h"

#include <QImage>
#include <QPainter>

#include <string>
#include <vector>

/// The filters and blend modes FLA and SWF files carry, applied to an instance that has been
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

/// Draws \a image at \a at with the instance's blend mode. Subtract and
/// invert, which Qt has no mode for, are made otherwise: invert is the
/// backdrop differenced with white under the instance's coverage; subtract
/// takes the instance off the backdrop channel by channel on an image (on a
/// widget it inverts, adds and inverts again). Alpha and erase cut into what
/// is under them, which only means something inside a parent with the Layer
/// blend mode (\a insideLayerGroup): alpha takes it down to its own opacity
/// wherever it is not clear, as the player does, and erase removes it by its
/// opacity. Elsewhere nothing of them shows, so nothing is drawn.
void composite(QPainter& painter, const QPoint& at, const QImage& image, const std::string& blendModeName,
    bool insideLayerGroup = false);

} // namespace Filters
