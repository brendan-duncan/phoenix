// The toolbox and the view as scripts see them: fl.selectTool and the
// tool names JSFL uses, fl.tools outside a tool's own script, and
// document.zoomFactor.

#include "extensions.h"
#include "model.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace jsfl {

namespace {

/// The names JSFL gives the built-in tools, and Phoenix's toolbox names
/// for them.
const std::pair<const char*, const char*> kToolNames[] = {
    {"arrow", "Selection"},
    {"bezierSelect", "Subselection"},
    {"freeXform", "Free Transform"},
    {"fillXform", "Gradient Transform"},
    {"lasso", "Lasso"},
    {"polygonLasso", "Polygon"},
    {"magicWand", "Magic Wand"},
    {"bone", "Bone"},
    {"boneBind", "Bind"},
    {"assetWarp", "Asset Warp"},
    {"pen", "Pen"},
    {"penplus", "Add Anchor Point"},
    {"penminus", "Delete Anchor Point"},
    {"penmodify", "Convert Anchor Point"},
    {"text", "Text"},
    {"line", "Line"},
    {"rect", "Rectangle"},
    {"oval", "Oval"},
    {"rectPrimitive", "Rectangle Primitive"},
    {"ovalPrimitive", "Oval Primitive"},
    {"polystar", "PolyStar"},
    {"width", "Width"},
    {"pencil", "Pencil"},
    {"paintBrush", "Paint Brush"},
    {"brush", "Brush"},
    {"inkBottle", "Ink Bottle"},
    {"bucket", "Paint Bucket"},
    {"eyeDropper", "Eyedropper"},
    {"eraser", "Eraser"},
    {"hand", "Hand"},
    {"rotation", "Rotation"},
    {"camera", "Camera"},
    {"magnifier", "Zoom"},
};

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The toolbox name for \a name: a JSFL name ("arrow") or a toolbox name
/// ("Selection", any case).
std::string toolboxName(Host& host, const std::string& name)
{
    for (const auto& [jsfl, phoenix] : kToolNames)
    {
        if (lower(name) == lower(jsfl))
            return phoenix;
    }
    for (const std::string& tool : host.tools())
    {
        if (lower(tool) == lower(name))
            return tool;
    }
    return name;
}

/// The name a script knows \a tool by: JSFL's for a built-in one.
std::string scriptName(const std::string& tool)
{
    for (const auto& [jsfl, phoenix] : kToolNames)
    {
        if (tool == phoenix)
            return jsfl;
    }
    return tool;
}

} // namespace

void describeFlashTools(ClassDef<Flash>& def)
{
    def.method("selectTool", 1, [](Flash& fl, const Args& args) {
        const std::string name = arg(args, 0).toString();
        Host& host = fl.session.host();
        if (!host.selectTool(toolboxName(host, name)))
            throw Error("There is no tool called " + name + ".");
        return Value();
    });
    // For fl.tools (the prelude's): the tools by the names scripts use, and
    // the one in use.
    def.method("__toolNames", 0, [](Flash& fl, const Args&) {
        Value names = Value::array();
        for (const std::string& tool : fl.session.host().tools())
            names.items.emplace_back(scriptName(tool));
        return names;
    });
    def.method("__activeTool", 0, [](Flash& fl, const Args&) {
        const std::string tool = fl.session.host().activeTool();
        return tool.empty() ? Value::null() : Value(scriptName(tool));
    });
}

void describeDocumentView(ClassDef<Document>& def)
{
    // 1 is 100%; Phoenix's stage goes from 1% to 3200%.
    def.prop("zoomFactor",
        [](Document& d) {
            d.data();
            return Value(d.session.host().zoom(d.document));
        },
        [](Document& d, const Value& v) {
            d.data();
            const double zoom = v.toNumber();
            if (!std::isfinite(zoom) || zoom <= 0)
                throw Error("zoomFactor is a magnification above 0, where 1 is 100%.");
            d.session.host().setZoom(d.document, std::clamp(zoom, 0.01, 32.0));
        });
}

} // namespace jsfl
