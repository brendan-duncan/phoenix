// Parameter names for keyword arguments in Python scripts. The object model
// (jsfl/model_*.cpp) takes arguments by position, as JSFL passes them; this
// table names the positions of the methods worth calling with keywords:
// those with optional arguments, or several of one kind.

#include "python_keywords.h"

#include <map>

namespace python {

namespace {

using Names = std::vector<std::string>;

/// "Class.method" for a method more than one class defines differently,
/// "method" for the rest.
const std::map<std::string, Names>& table()
{
    static const std::map<std::string, Names> names = {
        // fl.
        {"Flash.openDocument", {"fileURI"}},
        {"Flash.createDocument", {"docType"}},
        {"Flash.saveDocument", {"document", "fileURI"}},
        {"Flash.saveDocumentAs", {"document"}},
        {"Flash.closeDocument", {"document", "promptToSaveChanges"}},
        {"Flash.closeAll", {"promptToSave"}},
        {"Flash.setActiveWindow", {"document"}},
        {"Flash.findDocumentIndex", {"name"}},
        {"Flash.runScript", {"fileURI", "funcName"}},
        {"Flash.browseForFileURL", {"browseType", "title"}},
        {"Flash.browseForFolderURL", {"description"}},
        {"Flash.phoenixCall", {"name", "argumentsJson"}},
        {"Flash.publishDocument", {"flaURI", "publishProfile"}},
        {"outputPanel.save", {"fileURI", "appendToFile", "useSystemEncoding"}},

        // FLfile.
        {"FLfile.write", {"fileURI", "textToWrite", "appendMode"}},
        {"FLfile.copy", {"fileURI", "copyURI"}},
        {"FLfile.listFolder", {"folderURI", "filesOrDirectories"}},
        {"FLfile.setAttributes", {"fileURI", "attrs"}},

        // Document.
        {"Document.editScene", {"index"}},
        {"Document.enterEditMode", {"editMode"}},
        {"Document.clipPaste", {"inPlace"}},
        {"Document.setSelectionRect", {"rect", "replaceSelection", "contactSensitiveSelection"}},
        {"Document.traceBitmap", {"threshold", "minimumArea", "curveFit", "cornerThreshold"}},
        {"Document.convertToSymbol", {"type", "name", "registrationPoint"}},
        {"Document.swapElement", {"name"}},
        {"Document.arrange", {"arrangeMode"}},
        {"Document.align", {"alignMode", "useDocumentBounds"}},
        {"Document.distribute", {"distributeMode", "useDocumentBounds"}},
        {"Document.moveSelectionBy", {"distanceToMove"}},
        {"Document.scaleSelection", {"xScale", "yScale", "whichCorner"}},
        {"Document.rotateSelection", {"angle", "rotationPoint"}},
        {"Document.transformSelection", {"a", "b", "c", "d"}},
        {"Document.setElementProperty", {"property", "value"}},
        {"Document.getElementProperty", {"propertyName"}},
        {"Document.setInstanceAlpha", {"opacity"}},
        {"Document.setInstanceBrightness", {"brightness"}},
        {"Document.setInstanceTint", {"color", "strength"}},
        {"Document.getCustomFill", {"objectToFill"}},
        {"Document.getCustomStroke", {"locationOfStroke"}},
        {"Document.setCustomFill", {"fill"}},
        {"Document.setCustomStroke", {"stroke"}},
        {"Document.setFillColor", {"color"}},
        {"Document.setStrokeColor", {"color"}},
        {"Document.setStrokeSize", {"size"}},
        {"Document.setTransformationPoint", {"transformationPoint"}},
        {"Document.addNewText", {"boundingRectangle", "text"}},
        {"Document.addNewRectangle", {"boundingRectangle", "roundness", "suppressFill", "suppressStroke"}},
        {"Document.addNewOval", {"boundingRectangle", "suppressFill", "suppressStroke"}},
        {"Document.addNewLine", {"startPoint", "endpoint"}},
        {"Document.addNewPath", {"path", "suppressFill", "suppressStroke"}},
        {"Document.addItem", {"position", "item"}},
        {"Document.save", {"okToSaveAs"}},
        {"Document.saveAndCompact", {"okToSaveAs"}},
        {"Document.saveAsCopy", {"fileURI"}},
        {"Document.close", {"promptToSaveChanges"}},
        {"Document.exportPNG", {"fileURI", "currentPNGSettings", "currentFrame"}},
        {"Document.exportSWF", {"fileURI", "currentSettings"}},
        {"Document.xmlPanel", {"fileURI"}},

        // Data kept on documents, selections, items and elements.
        {"addDataToDocument", {"name", "type", "data"}},
        {"addDataToSelection", {"name", "type", "data"}},
        {"addData", {"name", "type", "data"}},
        {"setPersistentData", {"name", "type", "data"}},

        // Text.
        {"getTextString", {"startIndex", "endIndex"}},
        {"setTextString", {"text", "startIndex", "endIndex"}},
        {"getTextAttr", {"attrName", "startIndex", "endIndex"}},
        {"setTextAttr", {"attrName", "attrValue", "startIndex", "endIndex"}},

        // Timeline.
        {"Timeline.addNewLayer", {"name", "layerType", "addAbove"}},
        {"Timeline.deleteLayer", {"index"}},
        {"Timeline.setSelectedLayers", {"index", "replaceCurrentSelection"}},
        {"Timeline.findLayerIndex", {"name"}},
        {"Timeline.reorderLayer", {"layerToMove", "layerToPutItBy", "addBefore"}},
        {"Timeline.insertFrames", {"numFrames", "allLayers", "frameNumIndex"}},
        {"Timeline.removeFrames", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.insertKeyframe", {"frameNumIndex"}},
        {"Timeline.insertBlankKeyframe", {"frameNumIndex"}},
        {"Timeline.convertToKeyframes", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.convertToBlankKeyframes", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.clearKeyframes", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.clearFrames", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.createMotionTween", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.removeMotionTween", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.setSelectedFrames", {"startFrameIndex", "endFrameIndex", "replaceCurrentSelection"}},
        {"Timeline.copyFrames", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.cutFrames", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.pasteFrames", {"startFrameIndex", "endFrameIndex"}},
        {"Timeline.setFrameProperty", {"property", "value", "startFrameIndex", "endFrameIndex"}},
        {"Timeline.getFrameProperty", {"property", "startFrameIndex", "endFrameIndex"}},
        {"Timeline.setLayerProperty", {"property", "value", "layersToChange"}},
        {"Timeline.getLayerProperty", {"property"}},
        {"Timeline.expandFolder", {"expand", "recurseNestedParents", "index"}},
        {"Timeline.copyLayers", {"startLayerIndex", "endLayerIndex"}},
        {"Timeline.cutLayers", {"startLayerIndex", "endLayerIndex"}},
        {"Timeline.pasteLayers", {"layerIndex"}},
        {"Timeline.duplicateLayers", {"layerIndex"}},
        {"Layer.getFiltersAtFrame", {"frameIndex"}},
        {"Layer.setFiltersAtFrame", {"frameIndex", "filters"}},

        // Library.
        {"Library.selectItem", {"namePath", "replaceCurrentSelection", "select"}},
        {"Library.selectAll", {"select"}},
        {"Library.findItemIndex", {"namePath"}},
        {"Library.itemExists", {"namePath"}},
        {"Library.getItemType", {"namePath"}},
        {"Library.addNewItem", {"type", "namePath"}},
        {"Library.newFolder", {"folderPath"}},
        {"Library.deleteItem", {"namePath"}},
        {"Library.renameItem", {"name"}},
        {"Library.moveToFolder", {"folderPath", "itemToMove", "replace"}},
        {"Library.duplicateItem", {"namePath"}},
        {"Library.editItem", {"namePath"}},
        {"Library.addItemToDocument", {"position", "namePath"}},
        {"Library.getItemProperty", {"property"}},
        {"Library.setItemProperty", {"property", "value"}},
        {"Library.updateItem", {"namePath"}},
        {"Library.expandFolder", {"expand", "recurseNestedParents", "folderName"}},

        // Shapes.
        {"Edge.getControl", {"i"}},
        {"Edge.setControl", {"index", "x", "y"}},
        {"Edge.getHalfEdge", {"index"}},
        {"Edge.splitEdge", {"t"}},
        {"Vertex.setLocation", {"x", "y"}},
        {"Contour.getCubicSegmentPoints", {"cubicSegmentIndex"}},
        {"Contour.deleteEdge", {"edge"}},

        // XML dialogs.
        {"XMLUI.get", {"controlPropertyName"}},
        {"XMLUI.set", {"controlPropertyName", "value"}},
        {"XMLUI.getEnabled", {"controlID"}},
        {"XMLUI.setEnabled", {"controlID", "enable"}},
        {"XMLUI.getVisible", {"controlID"}},
        {"XMLUI.setVisible", {"controlID", "visible"}},
        {"XMLUI.getControlItemElements", {"controlPropertyName"}},
        {"XMLUI.setControlItemElements", {"controlPropertyName", "elementItemList"}},
        {"XMLUI.getControlItemElement", {"controlPropertyName"}},
        {"XMLUI.setControlItemElement", {"controlPropertyName", "elementItem"}},
    };
    return names;
}

} // namespace

const std::vector<std::string>* parameterNames(const std::string& owner, const std::string& method)
{
    const std::map<std::string, Names>& names = table();
    auto found = names.find(owner + "." + method);
    if (found == names.end())
        found = names.find(method);
    return found == names.end() ? nullptr : &found->second;
}

} // namespace python
