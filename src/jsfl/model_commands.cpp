// More of what scripts often call on a document: Test Movie, Convert Lines
// to Fills, primitive rectangles and ovals, a text's bounding rectangle,
// and livePreview.

#include "extensions.h"
#include "model.h"

#include "../data/frame.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/static_text.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"

#include <algorithm>
#include <map>

namespace jsfl {

namespace {

fla::DrawingStyle& toolbarOf(Document& d)
{
    d.data();
    if (fla::DrawingStyle* style = d.session.host().drawingStyle())
        return *style;
    static fla::DrawingStyle fallback;
    return fallback;
}

double edge(const Value& box, const char* name)
{
    const Value* value = box.field(name);
    return value ? value->toNumber() : 0.0;
}

/// \a element added to the current keyframe and selected, as the drawing
/// tools leave what they draw.
void addDrawn(Document& d, fla::Frame* frame, fla::Element* element, const char* name)
{
    fla::Selection& selection = d.session.host().selection(d.document);
    d.push(fla::CommandPtr(new fla::AddElementCommand(frame, element, name, &selection)));
    selection.select(element);
}

/// Whether live preview is on, by document; scripts set it around long
/// edits. Phoenix draws as it always does either way.
std::map<const fla::FLADocument*, bool>& livePreviews()
{
    static std::map<const fla::FLADocument*, bool> previews;
    return previews;
}

void runCommand(Document& d, const char* name, bool quiet)
{
    d.data();
    std::string error;
    const bool done = d.session.host().runCommand(d.document, name, error);
    d.session.invalidate(d.document);
    if (done)
        return;
    if (quiet)
        d.session.host().trace(error);
    else
        throw Error(error);
}

} // namespace

void describeDocumentCommands(ClassDef<Document>& def)
{
    // Test Movie plays the document as it is now; the script goes on once
    // its window is closed.
    def.method("testMovie", 0, [](Document& d, const Args&) {
        runCommand(d, "testMovie", true);
        return Value();
    });
    def.method("canTestMovie", 0, [](Document& d, const Args&) {
        d.data();
        return Value(d.session.host().canTestMovie(d.document));
    });
    def.method("testScene", 0, [](Document& d, const Args&) {
        runCommand(d, "testMovie", true);
        return Value();
    });
    def.method("convertLinesToFills", 0, [](Document& d, const Args&) {
        runCommand(d, "convertLinesToFills", false);
        return Value();
    });
    // The selection drawn into a new library bitmap that takes its place.
    def.method("convertSelectionToBitmap", 0, [](Document& d, const Args&) {
        runCommand(d, "convertSelectionToBitmap", false);
        return Value();
    });
    def.prop("livePreview",
        [](Document& d) {
            const auto found = livePreviews().find(d.document);
            return Value(found == livePreviews().end() ? true : found->second);
        },
        [](Document& d, const Value& v) { livePreviews()[d.document] = v.toBool(); });

    // addNewPrimitiveRectangle(rect, roundness, suppressFill, suppressStroke)
    def.method("addNewPrimitiveRectangle", 4, [](Document& d, const Args& args) {
        fla::Frame* frame = currentKeyframe(d.session, d.document);
        const fla::DrawingStyle& style = toolbarOf(d);
        const Value& box = arg(args, 0);
        auto* rectangle = new fla::RectanglePrimitive(frame);
        rectangle->rect = fla::Rect(fla::Point(std::min(edge(box, "left"), edge(box, "right")),
                                        std::min(edge(box, "top"), edge(box, "bottom"))),
            fla::Point(std::max(edge(box, "left"), edge(box, "right")), std::max(edge(box, "top"), edge(box, "bottom"))));
        if (!arg(args, 2).toBool())
            rectangle->fillStyle = style.createFill(rectangle);
        if (!arg(args, 3).toBool())
            rectangle->strokeStyle = style.createStroke(rectangle);
        const double radius = std::clamp(arg(args, 1).toNumber(), 0.0, 999.0);
        rectangle->topLeftRadius = rectangle->topRightRadius = radius;
        rectangle->bottomLeftRadius = rectangle->bottomRightRadius = radius;
        rectangle->localBounds = rectangle->rect;
        rectangle->bounds = rectangle->rect;
        addDrawn(d, frame, rectangle, "Rectangle Primitive");
        return Value();
    });
    // addNewPrimitiveOval(rect, suppressFill, suppressStroke)
    def.method("addNewPrimitiveOval", 3, [](Document& d, const Args& args) {
        fla::Frame* frame = currentKeyframe(d.session, d.document);
        const fla::DrawingStyle& style = toolbarOf(d);
        const Value& box = arg(args, 0);
        auto* oval = new fla::OvalPrimitive(frame);
        oval->rect = fla::Rect(fla::Point(std::min(edge(box, "left"), edge(box, "right")),
                                   std::min(edge(box, "top"), edge(box, "bottom"))),
            fla::Point(std::max(edge(box, "left"), edge(box, "right")), std::max(edge(box, "top"), edge(box, "bottom"))));
        if (!arg(args, 1).toBool())
            oval->fillStyle = style.createFill(oval);
        if (!arg(args, 2).toBool())
            oval->strokeStyle = style.createStroke(oval);
        oval->startAngle = style.ovalStartAngle;
        oval->endAngle = style.ovalEndAngle;
        oval->innerRadius = std::clamp(style.ovalInnerRadius, 0.0, 99.0);
        oval->localBounds = oval->rect;
        oval->bounds = oval->rect;
        addDrawn(d, frame, oval, "Oval Primitive");
        return Value();
    });

    // The selected text blocks set to the rectangle: where they start and
    // how wide and tall they are, no longer growing with their text.
    def.method("setTextRectangle", 1, [](Document& d, const Args& args) {
        const Value& box = arg(args, 0);
        const double left = edge(box, "left"), top = edge(box, "top");
        const double width = std::max(1.0, edge(box, "right") - left);
        const double height = std::max(1.0, edge(box, "bottom") - top);
        bool any = false;
        for (fla::Element* element : selectedElements(d.session, d.document))
        {
            auto* text = dynamic_cast<fla::StaticText*>(element);
            if (!text)
                continue;
            any = true;
            fla::Transform moved = text->transform;
            moved.tx = left;
            moved.ty = top;
            setTransform(d.session, d.document, text, moved, "Text Rectangle");
            d.push(fla::CommandPtr(new fla::SetPropertyCommand<double>(text->width, text->width, width, "Text Rectangle")), false);
            d.push(fla::CommandPtr(new fla::SetPropertyCommand<double>(text->height, text->height, height, "Text Rectangle")),
                false);
            d.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(text->autoExpand, text->autoExpand, false, "Text Rectangle")),
                false);
        }
        return Value(any);
    });
}

} // namespace jsfl
