#pragma once

#include <QImage>
#include <QList>
#include <QString>

/// The starter templates the start screen offers: ready-made documents with
/// art in them (a bouncing ball, a walk cycle, a lip-sync head, a title
/// card), each opened as a new untitled document to change. They live in
/// the app as :/templates/<id>/, an XFL folder with template.json (title
/// and summary) and thumbnail.png, made by tools/build_templates.py.
struct StarterTemplate
{
    QString id;
    QString title;
    QString summary;
    /// The template's folder, in the app.
    QString folder;
    QImage thumbnail;
};

namespace startertemplates {

/// Every starter template, in the order template.json's "order" gives.
QList<StarterTemplate> all();

/// A copy of template \a id to work on, in a folder of its own under the
/// temporary folder: the path of its DOMDocument.xml, or empty with
/// \a error saying why.
QString copyToWork(const QString& id, QString& error);

} // namespace startertemplates
