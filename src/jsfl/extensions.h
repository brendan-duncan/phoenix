#pragma once

#include "model.h"

namespace fla {
struct Filter;
class FillStyle;
class StrokeStyle;
}

/// Parts of the object model kept in files of their own, added to the
/// classes the model_*.cpp files declare: each describe function is called
/// from the class's table.
namespace jsfl {

/// Shape geometry (model_shape.cpp): vertices, edges, half-edges and
/// contours of a shape's drawing, read off its planar map, and
/// beginEdit/endEdit with deleteEdge, Vertex.setLocation, Edge.splitEdge
/// and Edge.setControl.
void describeShapeGeometry(ClassDef<Element>& def);

/// Persistent data (model_data.cpp): named values scripts keep on
/// elements, library items and the document, saved with the file.
void describeElementData(ClassDef<Element>& def);
void describeItemData(ClassDef<Item>& def);
void describeDocumentData(ClassDef<Document>& def);

/// Layers (model_layers.cpp): copy, cut, paste and duplicate whole layers,
/// a layer's folder, mask or guide set from a script, and filters on a
/// layer's frames.
void describeTimelineLayers(ClassDef<Timeline>& def);
void describeLayerExtras(ClassDef<Layer>& def);
/// Puts \a layer into \a parent (a folder, mask or guide), or out to the
/// top level for null.
void setParentLayer(Layer& layer, const Value& parent);

/// A filter as the plain object scripts read and write (model_element.cpp).
Value filterToValue(const fla::Filter& filter);
fla::Filter filterFromValue(const Value& value);

/// The fill and stroke (model_style.cpp): the toolbar's and the selected
/// shapes', set and read by setFillColor, setStroke, setStrokeStyle,
/// setCustomStroke and their kin.
void describeDocumentStyle(ClassDef<Document>& def);
/// A fill or stroke as the plain object scripts read (model_shape.cpp);
/// null gives "noFill" or "noStroke".
Value fillToValue(const fla::FillStyle* fill);
Value strokeToValue(const fla::StrokeStyle* stroke);

/// The toolbox and the view (model_view.cpp): fl.selectTool, what
/// fl.tools reads outside a tool's script, and document.zoomFactor.
void describeFlashTools(ClassDef<Flash>& def);
void describeDocumentView(ClassDef<Document>& def);

/// Importing (model_import.cpp): document.importFile, documents and
/// movies among what it takes, and importEmbeddedSWF.
void describeDocumentImport(ClassDef<Document>& def);

/// Publish profiles (model_profiles.cpp): publishProfiles,
/// currentPublishProfile, adding, renaming and deleting them, exporting
/// and importing them as XML, asVersion and docClass.
void describeDocumentProfiles(ClassDef<Document>& def);

/// More of the document (model_commands.cpp): testMovie, testScene,
/// convertLinesToFills, livePreview, primitive rectangles and ovals, and
/// setTextRectangle.
void describeDocumentCommands(ClassDef<Document>& def);

/// Publishing (model_publish.cpp): document.exportSWF, document.publish,
/// fl.publishDocument and FLfile.runCommandLine.
void describeDocumentPublishing(ClassDef<Document>& def);
void describeFlashPublishing(ClassDef<Flash>& def);
void describeFileSystemProcesses(ClassDef<FileSystem>& def);

} // namespace jsfl
