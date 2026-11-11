#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class PhoenixView;

namespace fla {
class Document;
}

/// File > Export > Export Lottie: the movie as a Lottie animation (JSON, as
/// lottie-web, Skottie and the mobile players read it), for web and app
/// developers.
///
/// What is exported is what the HTML5 publish works out
/// (Html5Publish::exportContent): each frame's objects with tweens worked
/// out, shapes as paths with their fills and strokes, and pictures for what
/// only Phoenix draws. Every symbol is opened up into the shapes, bitmaps and
/// text it shows, each a layer of the animation with its place, size, turn
/// and opacity held frame by frame, so classic, shape and motion tweens,
/// graphics' loops and movie clips' own timelines all play as in Phoenix.
/// Masks become track mattes; text is outlines; pictures are embedded.
namespace LottieExport {

/// The animation for \a document as \a view draws it, as JSON; \a notes gets
/// what could not go as it is (color effects other than alpha, filters,
/// sounds, scripts...). Empty with \a error set when it could not be made.
QByteArray animation(PhoenixView& view, const fla::Document& document, const QString& name, QStringList& notes,
    QString& error);

/// Writes animation() to \a path. False with \a error set when it could
/// not.
bool write(PhoenixView& view, const fla::Document& document, const QString& path, QStringList& notes, QString& error);

/// Lottie's transform (position, scale in percent, rotation and skew in
/// degrees, skew axis 0) for the matrix (a, b, c, d, tx, ty), x' = a x + c y +
/// tx, y' = b x + d y + ty: rotation, then skew, then scale, as Lottie
/// composes them. False for a matrix that flattens to a line.
bool decompose(double a, double b, double c, double d, double& rotation, double& scaleX, double& scaleY, double& skew);

} // namespace LottieExport
