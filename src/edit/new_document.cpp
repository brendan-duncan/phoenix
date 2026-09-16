#include "new_document.h"

#include "../data/fla_document.h"

#include <string>

namespace fla {

namespace {

/// What Animate writes for an ordinary keyframe. The value is a bit field the
/// format never documents; carrying the same one through means a new document
/// and a loaded one are indistinguishable to anything reading frames.
constexpr int kNormalKeyMode = 9728;

} // namespace

FLADocument* createEmptyDocument(int width, int height, double frameRate)
{
    FLADocument* fla = new FLADocument(nullptr);

    Document* document = new Document(fla);
    fla->document = document;

    document->width = width;
    document->height = height;
    document->frameRate = frameRate;

    // Written into DOMDocument.xml so the file says what made it. The version
    // is the XFL schema's, not ours, and has to match what the format expects.
    document->xflVersion = "23.0";
    document->creatorInfo = "Phoenix";

    // A 1-based index into the timelines, as a string, which is how the format
    // names the scene that is open.
    document->currentTimeline = "1";

    Timeline* timeline = new Timeline(document);
    timeline->name = "Scene 1";
    document->timelines.push_back(timeline);

    Layer* layer = new Layer(timeline);
    layer->name = "Layer_1";
    // Cyan, which is the colour Animate gives a document's first layer.
    layer->color[0] = 0;
    layer->color[1] = 255;
    layer->color[2] = 255;
    layer->color[3] = 255;
    layer->current = "true";
    layer->selected = true;
    timeline->layers.push_back(layer);

    Frame* frame = new Frame(layer);
    frame->index = 0;
    frame->duration = 1;
    frame->keyMode = std::to_string(kNormalKeyMode);
    layer->frames.push_back(frame);

    // The parser derives these after reading a layer's frames, and frame
    // resolution reads them, so a built document has to fill them in too.
    layer->firstFrame = frame->index;
    layer->lastFrame = frame->index + frame->duration;

    return fla;
}

} // namespace fla
