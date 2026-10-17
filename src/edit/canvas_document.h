#pragma once

#include <string>

namespace fla {

class Document;

/// HTML5 Canvas documents: their scripts are JavaScript (CreateJS's API)
/// rather than ActionScript. A document is one when its publish settings'
/// current profile publishes HTML5 Canvas, which keeps that format's
/// settings as a PublishProperties group with the id kPublishFormat. Free
/// of Qt.
namespace canvas {

/// The id of the HTML5 Canvas format's settings in PublishSettings.xml.
constexpr const char* kPublishFormat = "JavaScript/HTML";

/// Whether the current profile of \a publishSettings (a PublishSettings.xml)
/// publishes HTML5 Canvas.
bool publishesCanvas(const std::string& publishSettings);

/// Whether \a document is an HTML5 Canvas document.
bool isCanvasDocument(const Document& document);

/// A PublishSettings.xml for a new HTML5 Canvas document named \a name:
/// the Canvas format on, the SWF formats off.
std::string newPublishSettings(const std::string& name);

/// Makes \a document (a new one, named \a name) an HTML5 Canvas document:
/// its publish settings are newPublishSettings's.
void makeCanvasDocument(Document& document, const std::string& name);

/// What kind of document it is, as Document properties name it: "HTML5
/// Canvas", "ActionScript 3.0" (or 1.0 or 2.0, as its publish settings
/// give), or "ActionScript" when they say none.
std::string documentType(const Document& document);

} // namespace canvas
} // namespace fla
