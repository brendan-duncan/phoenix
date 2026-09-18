#include "test_util.h"

#include "../src/data/fla_document.h"
#include "../src/edit/new_document.h"

#include <memory>

using namespace fla;

namespace {

std::unique_ptr<FLADocument> makeEmpty()
{
    return std::unique_ptr<FLADocument>(createEmptyDocument());
}

} // namespace

TEST(new_document_has_a_document)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();

    CHECK(fla != nullptr);
    CHECK(fla->document != nullptr);
}

TEST(new_document_uses_the_default_stage)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();

    CHECK(fla->document->width == kDefaultDocumentWidth);
    CHECK(fla->document->height == kDefaultDocumentHeight);
    CHECK_NEAR(fla->document->frameRate, kDefaultDocumentFrameRate);
}

TEST(new_document_honours_a_requested_stage)
{
    std::unique_ptr<FLADocument> fla(createEmptyDocument(1280, 720, 30.0));

    CHECK(fla->document->width == 1280);
    CHECK(fla->document->height == 720);
    CHECK_NEAR(fla->document->frameRate, 30.0);
}

/// Animate puts the vanishing point at the middle of the stage, so it has to
/// follow the size rather than keep the model's own default.
TEST(new_document_centres_the_vanishing_point)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();

    CHECK(fla->document->vanishingPoint3DX == kDefaultDocumentWidth / 2);
    CHECK(fla->document->vanishingPoint3DY == kDefaultDocumentHeight / 2);

    std::unique_ptr<FLADocument> other(createEmptyDocument(1280, 720, 30.0));
    CHECK(other->document->vanishingPoint3DX == 640);
    CHECK(other->document->vanishingPoint3DY == 360);
}

TEST(new_document_has_one_scene_one_layer_one_frame)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();
    const Document* document = fla->document;

    CHECK(document->timelines.size() == 1);

    const Timeline* timeline = document->timelines[0];
    CHECK(timeline->name == "Scene 1");
    CHECK(timeline->layers.size() == 1);

    const Layer* layer = timeline->layers[0];
    CHECK(layer->frames.size() == 1);
    CHECK(layer->frames[0]->elements.empty());
}

/// The layer a tool draws on has to be one that passes the visible, unlocked,
/// non-folder, non-guide test the view applies when it picks a frame.
TEST(new_document_layer_is_drawable)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();
    const Layer* layer = fla->document->timelines[0]->layers[0];

    CHECK(layer->visible);
    CHECK(layer->isVisible());
    CHECK(!layer->locked);
    CHECK(layer->layerType == Layer::Type::Normal);
}

/// The parser fills these in after reading a layer's frames, and frame
/// resolution reads them. A built document that left them at zero would resolve
/// to no frame at all, so nothing could be drawn on it.
TEST(new_document_layer_frame_range_is_filled_in)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();
    const Layer* layer = fla->document->timelines[0]->layers[0];

    CHECK(layer->firstFrame == 0);
    CHECK(layer->lastFrame == 1);
    CHECK(layer->lastFrame - layer->firstFrame == layer->frames[0]->duration);
}

/// The timelines are what the view walks, and the document has to say which of
/// them is open. The format numbers them from one, as a string.
TEST(new_document_names_the_current_timeline)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();

    CHECK(fla->document->currentTimeline == "1");
}

TEST(new_document_records_what_made_it)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();

    CHECK(!fla->document->xflVersion.empty());
    CHECK(!fla->document->creatorInfo.empty());
}

TEST(new_document_starts_with_a_white_stage)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();
    const uint8_t* background = fla->document->backgroundColor;

    CHECK(background[0] == 255);
    CHECK(background[1] == 255);
    CHECK(background[2] == 255);
    CHECK(background[3] == 255);
}

TEST(new_document_has_no_library)
{
    std::unique_ptr<FLADocument> fla = makeEmpty();

    CHECK(fla->document->symbolList == nullptr);
    CHECK(fla->document->symbolInstances.empty());
}

/// Two documents must not share anything, or editing one would change the
/// other and deleting one would leave the other holding freed memory.
TEST(new_documents_are_independent)
{
    std::unique_ptr<FLADocument> first = makeEmpty();
    std::unique_ptr<FLADocument> second = makeEmpty();

    CHECK(first->document != second->document);
    CHECK(first->document->timelines[0] != second->document->timelines[0]);

    const Timeline* firstTimeline = first->document->timelines[0];
    first.reset();

    // The survivor is untouched by the other going away.
    CHECK(second->document->timelines.size() == 1);
    CHECK(second->document->timelines[0] != firstTimeline);
    CHECK(second->document->timelines[0]->layers.size() == 1);
}
