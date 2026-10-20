// The display engine ActionScript 3 movies draw with (src/play/display):
// pixels, drawing, the display list and its events, with no renderer.
#include "test_util.h"

#include "../src/play/display/bitmap_data.h"
#include "../src/play/display/bitmap_filter.h"
#include "../src/play/display/display_object.h"
#include "../src/play/display/graphics.h"
#include "../src/play/display/stage.h"
#include "../src/play/display/text_field.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace fla::display;

namespace {

std::shared_ptr<Sprite> box(double x, double y, double w, double h, uint32_t color = 0xff0000)
{
    auto sprite = std::make_shared<Sprite>();
    sprite->graphics.beginFill(color);
    sprite->graphics.drawRect(0, 0, w, h);
    sprite->graphics.endFill();
    sprite->setX(x);
    sprite->setY(y);
    return sprite;
}

} // namespace

TEST(display_bitmap_data_keeps_pixels_premultiplied)
{
    BitmapData bitmap(4, 4, true, 0);
    // Opaque colors come back as they went in.
    bitmap.setPixel32(0, 0, 0xff123456);
    CHECK(bitmap.getPixel32(0, 0) == 0xff123456u);
    CHECK(bitmap.getPixel(0, 0) == 0x123456u);
    // Half see-through: still exact at this alpha.
    bitmap.setPixel32(1, 0, 0x80123456);
    CHECK(bitmap.getPixel32(1, 0) == 0x80123456u);
    // Nearly see-through: the premultiplied channels lose what Flash
    // Player's lose.
    bitmap.setPixel32(2, 0, 0x10123456);
    CHECK(bitmap.getPixel32(2, 0) == 0x10103050u);
    // Clear: the color is gone.
    bitmap.setPixel32(3, 0, 0x00ffffff);
    CHECK(bitmap.getPixel32(3, 0) == 0u);
    // setPixel keeps the alpha there.
    bitmap.setPixel(1, 0, 0xffffff);
    CHECK((bitmap.getPixel32(1, 0) >> 24) == 0x80u);
    CHECK(bitmap.getPixel(1, 0) == 0xffffffu);
    // Off the edge: nothing read, nothing written.
    bitmap.setPixel32(4, 0, 0xffffffff);
    bitmap.setPixel32(-1, 0, 0xffffffff);
    CHECK(bitmap.getPixel32(4, 0) == 0u);
    CHECK(bitmap.getPixel(0, 4) == 0u);
    // Opaque bitmaps ignore alpha.
    BitmapData opaque(2, 2, false, 0x00ff0000);
    CHECK(opaque.getPixel32(0, 0) == 0xffff0000u);
    opaque.setPixel32(0, 0, 0x10123456);
    CHECK(opaque.getPixel32(0, 0) == 0xff123456u);
    // Sizes Flash Player refuses make none.
    BitmapData huge(9000, 10, true, 0);
    CHECK(huge.disposed());
}

TEST(display_bitmap_data_fills_copies_and_floods)
{
    BitmapData bitmap(10, 10, true, 0xffffffff);
    bitmap.fillRect(Rect(2, 2, 3, 3), 0xff0000ff);
    CHECK(bitmap.getPixel32(2, 2) == 0xff0000ffu);
    CHECK(bitmap.getPixel32(4, 4) == 0xff0000ffu);
    CHECK(bitmap.getPixel32(5, 5) == 0xffffffffu);

    // copyPixels: as they are, then blended with mergeAlpha.
    BitmapData source(2, 2, true, 0x80ff0000);
    BitmapData target(4, 4, true, 0xff0000ff);
    target.copyPixels(source, source.rect(), Point{1, 1});
    CHECK(target.getPixel32(1, 1) == 0x80ff0000u);
    CHECK(target.getPixel32(0, 0) == 0xff0000ffu);
    target.fillRect(target.rect(), 0xff0000ff);
    target.copyPixels(source, source.rect(), Point{1, 1}, nullptr, Point(), true);
    const uint32_t merged = target.getPixel32(1, 1);
    CHECK((merged >> 24) == 0xffu);
    CHECK(((merged >> 16) & 0xff) > 0x70 && ((merged >> 16) & 0xff) < 0x90);
    CHECK((merged & 0xff) > 0x70 && (merged & 0xff) < 0x90);

    // A flood stops at another color.
    BitmapData canvas(8, 8, true, 0xffffffff);
    for (int i = 0; i < 8; ++i)
        canvas.setPixel32(4, i, 0xff000000);
    canvas.floodFill(0, 0, 0xff00ff00);
    CHECK(canvas.getPixel32(0, 0) == 0xff00ff00u);
    CHECK(canvas.getPixel32(3, 7) == 0xff00ff00u);
    CHECK(canvas.getPixel32(4, 3) == 0xff000000u);
    CHECK(canvas.getPixel32(5, 0) == 0xffffffffu);

    // Bytes out and in again.
    std::vector<uint8_t> bytes = canvas.getPixels(Rect(0, 0, 2, 1));
    CHECK(bytes.size() == 8u);
    CHECK(bytes[0] == 0xff && bytes[1] == 0x00 && bytes[2] == 0xff && bytes[3] == 0x00);
    BitmapData copy(2, 1, true, 0);
    size_t position = 0;
    CHECK(copy.setPixels(copy.rect(), bytes.data(), bytes.size(), position));
    CHECK(position == 8u);
    CHECK(copy.getPixel32(1, 0) == 0xff00ff00u);

    // threshold, colorTransform, scroll, clone.
    BitmapData gray(4, 1, false, 0);
    gray.setPixel(0, 0, 0x101010);
    gray.setPixel(1, 0, 0x808080);
    gray.setPixel(2, 0, 0xf0f0f0);
    const uint32_t counted = gray.threshold(gray, gray.rect(), Point(), ">", 0x00800000, 0xffff0000, 0x00ff0000);
    CHECK(counted == 1u);
    CHECK(gray.getPixel(2, 0) == 0xff0000u);
    ColorTransform half;
    half.redMultiplier = 0.5;
    gray.colorTransform(gray.rect(), half);
    CHECK(gray.getPixel(2, 0) == 0x7f0000u);
    gray.scroll(1, 0);
    CHECK(gray.getPixel(1, 0) == 0x081010u);
    std::shared_ptr<BitmapData> twin = gray.clone();
    CHECK(twin->getPixel(3, 0) == gray.getPixel(3, 0));
    twin->dispose();
    CHECK(twin->disposed());
    CHECK(twin->getPixel(0, 0) == 0u);
}

TEST(display_bitmap_data_draws_bitmaps_and_filters)
{
    BitmapData source(2, 2, true, 0xffff0000);
    BitmapData target(6, 6, true, 0);
    Matrix scale;
    scale.scale(2, 2);
    scale.translate(1, 1);
    target.draw(source, scale);
    CHECK(target.getPixel32(1, 1) == 0xffff0000u);
    CHECK(target.getPixel32(4, 4) == 0xffff0000u);
    CHECK(target.getPixel32(5, 5) == 0u);
    CHECK(target.getPixel32(0, 0) == 0u);

    // A color matrix that halves alpha, as bunnyPaint's layer opacity does.
    BitmapData layer(2, 2, true, 0xff00ff00);
    layer.applyFilter(layer, layer.rect(), Point(),
        BitmapFilter::colorMatrix({1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, 0}));
    CHECK(layer.getPixel32(0, 0) == 0x8000ff00u);

    // A blur spreads a dot.
    BitmapData dot(9, 9, true, 0);
    dot.setPixel32(4, 4, 0xffffffff);
    dot.applyFilter(dot, dot.rect(), Point(), BitmapFilter::blur(4, 4, 1));
    CHECK((dot.getPixel32(3, 4) >> 24) > 0u);
    CHECK((dot.getPixel32(4, 4) >> 24) < 0xffu);
}

TEST(display_bitmap_data_pixel_loops_are_fast)
{
    // A brush stroke reads and writes about a million pixels.
    BitmapData bitmap(1000, 1000, true, 0);
    const auto start = std::chrono::steady_clock::now();
    uint32_t sum = 0;
    for (int y = 0; y < 1000; ++y)
        for (int x = 0; x < 1000; ++x)
        {
            sum += bitmap.getPixel(x, y);
            bitmap.setPixel32(x, y, 0x80000000u | (static_cast<uint32_t>(x) << 8) | static_cast<uint32_t>(y & 0xff));
        }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("    1M getPixel + setPixel32: %.1f ms\n", seconds * 1000.0);
    CHECK(sum == 0u);
    CHECK(seconds < 0.5);
}

TEST(display_graphics_bounds_and_hits)
{
    Graphics g;
    g.lineStyle(2, 0);
    g.beginFill(0xff0000);
    g.drawRect(0, 0, 100, 50);
    g.endFill();
    const Rect withStrokes = g.bounds(true);
    CHECK_NEAR(withStrokes.x, -1.0);
    CHECK_NEAR(withStrokes.width, 102.0);
    const Rect bare = g.bounds(false);
    CHECK_NEAR(bare.x, 0.0);
    CHECK_NEAR(bare.height, 50.0);
    // The stroke goes after the fill it was drawn with.
    CHECK(g.paths().size() == 2u);
    CHECK(g.paths()[0].kind == GraphicsPath::Kind::Fill);
    CHECK(g.paths()[1].kind == GraphicsPath::Kind::Line);
    CHECK(g.hits(50, 25));
    CHECK(g.hits(100.5, 25));
    CHECK(!g.hits(150, 25));

    // Two squares in one fill: where they overlap is a hole (even-odd).
    Graphics holes;
    holes.beginFill(0);
    holes.drawRect(0, 0, 20, 20);
    holes.drawRect(10, 10, 20, 20);
    holes.endFill();
    CHECK(holes.hits(5, 5));
    CHECK(!holes.hits(15, 15));
    CHECK(holes.hits(25, 25));

    Graphics circle;
    circle.beginFill(0);
    circle.drawCircle(50, 50, 10);
    const Rect round = circle.bounds();
    CHECK(std::fabs(round.x - 40.0) < 0.2);
    CHECK(std::fabs(round.width - 20.0) < 0.4);
    CHECK(circle.hits(50, 50));
    CHECK(!circle.hits(58, 58));
    circle.clear();
    CHECK(circle.empty());

    // A gradient box: 1638.4 units across the gradient square.
    Matrix m;
    m.createGradientBox(100, 50, 0, 10, 20);
    CHECK_NEAR(m.a, 100.0 / 1638.4);
    CHECK_NEAR(m.tx, 60.0);
    CHECK_NEAR(m.ty, 45.0);
}

TEST(display_list_orders_children_and_tells_them)
{
    auto stage = std::make_shared<Stage>(400, 300);
    auto root = std::make_shared<MovieClip>();
    stage->addChild(root);
    stage->setRootObject(root.get());
    auto a = std::make_shared<Sprite>();
    auto b = std::make_shared<Sprite>();
    auto c = std::make_shared<Shape>();
    a->setName("a");
    b->setName("b");
    std::vector<std::string> heard;
    a->addEventListener(events::kAdded, [&](Event& e) { heard.push_back("added " + static_cast<DisplayObject*>(e.target)->name()); });
    a->addEventListener(events::kAddedToStage, [&](Event&) { heard.push_back("a on stage"); });
    root->addEventListener(events::kAdded, [&](Event& e) {
        if (e.eventPhase == Event::Phase::Bubbling)
            heard.push_back("root heard added");
    });
    CHECK(root->addChild(a) == DisplayError::None);
    CHECK(heard.size() == 3u && heard[0] == "added a" && heard[1] == "root heard added" && heard[2] == "a on stage");
    CHECK(a->stage() == stage.get());
    CHECK(a->root() == root.get());
    root->addChild(b);
    root->addChildAt(c, 0);
    CHECK(root->getChildIndex(c.get()) == 0 && root->getChildIndex(a.get()) == 1 && root->getChildIndex(b.get()) == 2);
    CHECK(root->setChildIndex(c.get(), 2) == DisplayError::None);
    CHECK(root->getChildAt(2) == c);
    CHECK(root->swapChildren(a.get(), b.get()) == DisplayError::None);
    CHECK(root->getChildAt(0) == b);
    CHECK(root->getChildByName("a") == a);
    // The errors scripts throw.
    CHECK(root->addChildAt(std::make_shared<Shape>(), 5) == DisplayError::IndexOutOfRange);
    CHECK(a->addChild(a) == DisplayError::AddSelf);
    a->addChild(std::make_shared<Sprite>());
    CHECK(static_cast<DisplayObjectContainer*>(a->getChildAt(0).get())->addChild(root) == DisplayError::AddAncestor);
    CHECK(root->removeChild(std::make_shared<Shape>().get()) == DisplayError::NotAChild);
    // Moving a child from one parent to another takes it off the first.
    b->addChild(c);
    CHECK(root->numChildren() == 2);
    CHECK(c->parent() == b.get());
    // Off the stage, and the events say so.
    heard.clear();
    a->addEventListener(events::kRemovedFromStage, [&](Event&) { heard.push_back("a off stage"); });
    root->removeChild(a.get());
    CHECK(heard.size() == 1u && heard[0] == "a off stage");
    CHECK(a->stage() == nullptr);
    CHECK(a->root() == a.get());

    // Timeline children by depth; script children over them.
    auto clip = std::make_shared<MovieClip>();
    auto scripted = std::make_shared<Sprite>();
    clip->addChild(scripted);
    auto low = std::make_shared<Shape>();
    low->timeline = TimelinePlacement{nullptr, nullptr, 1, "low"};
    auto high = std::make_shared<Shape>();
    high->timeline = TimelinePlacement{nullptr, nullptr, 5, "high"};
    auto middle = std::make_shared<Shape>();
    middle->timeline = TimelinePlacement{nullptr, nullptr, 3, "middle"};
    clip->placeTimelineChild(low);
    clip->placeTimelineChild(high);
    CHECK(clip->getChildIndex(scripted.get()) == 0);
    CHECK(clip->getChildIndex(high.get()) == 2);
    clip->placeTimelineChild(middle);
    CHECK(clip->getChildIndex(middle.get()) == 2);
    CHECK(clip->getChildIndex(high.get()) == 3);
}

TEST(display_events_capture_target_and_bubble)
{
    auto stage = std::make_shared<Stage>();
    auto outer = std::make_shared<Sprite>();
    auto inner = std::make_shared<Sprite>();
    stage->addChild(outer);
    outer->addChild(inner);
    std::vector<std::string> order;
    outer->addEventListener("ping", [&](Event& e) { order.push_back("outer capture " + std::to_string(int(e.eventPhase))); }, true);
    outer->addEventListener("ping", [&](Event& e) { order.push_back("outer bubble " + std::to_string(int(e.eventPhase))); });
    inner->addEventListener("ping", [&](Event&) { order.push_back("inner low"); }, false, -1);
    inner->addEventListener("ping", [&](Event& e) {
        order.push_back("inner high");
        CHECK(e.currentTarget == inner.get());
    }, false, 5);
    stage->addEventListener("ping", [&](Event&) { order.push_back("stage bubble"); });
    Event ping("ping", true, true);
    CHECK(inner->dispatchEvent(ping));
    CHECK(order.size() == 5u);
    CHECK(order[0] == "outer capture 1");
    CHECK(order[1] == "inner high");
    CHECK(order[2] == "inner low");
    CHECK(order[3] == "outer bubble 3");
    CHECK(order[4] == "stage bubble");

    // stopImmediatePropagation stops the rest, even at the same object;
    // preventDefault is told back.
    order.clear();
    const int key = 0;
    inner->addEventListener("pong", [&](Event& e) {
        order.push_back("first");
        e.stopImmediatePropagation();
        e.preventDefault();
    }, false, 0, &key);
    inner->addEventListener("pong", [&](Event&) { order.push_back("second"); });
    outer->addEventListener("pong", [&](Event&) { order.push_back("outer"); });
    Event pong("pong", true, true);
    CHECK(!inner->dispatchEvent(pong));
    CHECK(order.size() == 1u);
    // The same listener is not added twice, and comes off by its key.
    CHECK(inner->addEventListener("pong", [](Event&) {}, false, 0, &key) == 0u);
    inner->removeEventListener("pong", &key);
    order.clear();
    Event again("pong", true);
    inner->dispatchEvent(again);
    CHECK(order.size() == 2u && order[0] == "second" && order[1] == "outer");
    CHECK(inner->willTrigger("ping"));
    CHECK(!inner->hasEventListener("nothing"));

    // An event dispatcher on its own, and enterFrame to whoever listens.
    auto plain = std::make_shared<EventDispatcher>();
    int frames = 0;
    plain->addEventListener(events::kEnterFrame, [&](Event&) { ++frames; });
    auto offStage = std::make_shared<Sprite>();
    offStage->addEventListener(events::kEnterFrame, [&](Event&) { ++frames; });
    stage->enterFrame();
    CHECK(frames == 2);
    offStage.reset();
    stage->enterFrame();
    CHECK(frames == 3);
}

TEST(display_transforms_bounds_and_hit_tests)
{
    auto stage = std::make_shared<Stage>();
    auto parent = box(100, 50, 10, 10);
    stage->addChild(parent);
    auto child = box(20, 0, 40, 20, 0x00ff00);
    parent->addChild(child);
    parent->setScaleX(2.0);
    CHECK_NEAR(parent->width(), 120.0);
    const Point global = child->localToGlobal({0, 0});
    CHECK_NEAR(global.x, 140.0);
    CHECK_NEAR(global.y, 50.0);
    const Point local = child->globalToLocal({160, 60});
    CHECK_NEAR(local.x, 10.0);
    CHECK_NEAR(local.y, 10.0);
    const Rect bounds = child->getBounds(stage.get());
    CHECK_NEAR(bounds.x, 140.0);
    CHECK_NEAR(bounds.width, 80.0);
    CHECK(child->hitTestPoint(150, 55, true));
    CHECK(!child->hitTestPoint(125, 55, true));
    CHECK(parent->hitTestPoint(105, 55, true));
    // width sets the scale to fit.
    child->setWidth(80);
    CHECK_NEAR(child->scaleX(), 2.0);
    child->setRotation(90);
    CHECK_NEAR(child->rotation(), 90.0);
    CHECK_NEAR(child->scaleX(), 2.0);
    child->setRotation(270);
    CHECK_NEAR(child->rotation(), -90.0);
    // A mask cuts what hits.
    auto masked = box(0, 200, 100, 100);
    auto mask = box(0, 200, 50, 100);
    stage->addChild(masked);
    stage->addChild(mask);
    masked->setMask(mask);
    CHECK(masked->hitTestPoint(25, 250, true));
    CHECK(!masked->hitTestPoint(75, 250, true));
    CHECK(mask->maskedObject() == masked.get());
    // alpha is the color transform's multiplier.
    masked->setAlpha(0.25);
    CHECK_NEAR(masked->colorTransform().alphaMultiplier, 0.25);
}

TEST(display_stage_sends_mouse_events)
{
    auto stage = std::make_shared<Stage>(400, 300);
    auto back = box(0, 0, 400, 300, 0xffffff);
    auto button = box(50, 50, 100, 40);
    button->buttonMode = true;
    auto label = std::make_shared<Shape>();
    label->graphics.beginFill(0);
    label->graphics.drawRect(10, 10, 20, 20);
    button->addChild(label);
    stage->addChild(back);
    stage->addChild(button);
    std::vector<std::string> heard;
    const auto listen = [&](const std::shared_ptr<Sprite>& sprite, const std::string& name) {
        for (const char* type : {events::kMouseDown, events::kMouseUp, events::kClick, events::kDoubleClick,
                 events::kMouseOver, events::kMouseOut, events::kRollOver, events::kRollOut})
            sprite->addEventListener(type, [&heard, name, type](Event& e) {
                if (e.eventPhase != Event::Phase::Bubbling)
                    heard.push_back(name + " " + type);
            });
    };
    listen(back, "back");
    listen(button, "button");
    stage->pointerMove(10, 10);
    CHECK(stage->hovered() == back.get());
    heard.clear();
    // Onto the button's shape: its sprite is the target.
    stage->pointerMove(65, 65);
    CHECK(stage->hovered() == button.get());
    CHECK(stage->handCursor());
    CHECK(heard.size() == 4u && heard[0] == "back mouseOut" && heard[1] == "back rollOut" && heard[2] == "button rollOver" &&
        heard[3] == "button mouseOver");
    heard.clear();
    stage->pointerDown(65, 65, {}, 1.0);
    stage->pointerUp(65, 65);
    CHECK(heard.size() == 3u && heard[0] == "button mouseDown" && heard[1] == "button mouseUp" && heard[2] == "button click");
    // Down on one, up on another: no click.
    heard.clear();
    stage->pointerDown(65, 65, {}, 2.0);
    stage->pointerUp(300, 200);
    CHECK(std::find(heard.begin(), heard.end(), "button click") == heard.end());
    CHECK(std::find(heard.begin(), heard.end(), "back click") == heard.end());
    // Two quick clicks on an object that wants them: a double click.
    button->doubleClickEnabled = true;
    stage->pointerMove(65, 65);
    heard.clear();
    stage->pointerDown(65, 65, {}, 3.0);
    stage->pointerUp(65, 65);
    stage->pointerDown(65, 65, {}, 3.2);
    stage->pointerUp(65, 65);
    CHECK(std::count(heard.begin(), heard.end(), "button click") == 1);
    CHECK(std::count(heard.begin(), heard.end(), "button doubleClick") == 1);
    // A sprite that does not take the mouse lets the pointer through to
    // what is under it.
    button->mouseEnabled = false;
    CHECK(stage->hitTarget(65, 65) == back.get());
    button->mouseEnabled = true;
    // mouseChildren off: the container takes it whole.
    auto group = std::make_shared<Sprite>();
    auto part = box(0, 0, 10, 10);
    group->addChild(part);
    group->setX(300);
    stage->addChild(group);
    CHECK(stage->hitTarget(305, 5) == part.get());
    group->mouseChildren = false;
    CHECK(stage->hitTarget(305, 5) == group.get());

    // Dragging follows the pointer within its bounds.
    Rect bounds(0, 0, 100, 100);
    stage->pointerMove(60, 60);
    stage->startDrag(*button, false, &bounds);
    stage->pointerMove(80, 70);
    CHECK_NEAR(button->x(), 70.0);
    CHECK_NEAR(button->y(), 60.0);
    stage->pointerMove(400, 400);
    CHECK_NEAR(button->x(), 100.0);
    stage->stopDrag();
}

TEST(display_art_over_a_button_leaves_it_the_pointer)
{
    // A panel: buttons in a sprite, and the icons drawn over them as one
    // plain shape in the same sprite.
    auto stage = std::make_shared<Stage>(400, 300);
    auto panel = std::make_shared<Sprite>();
    auto button = box(50, 50, 40, 40);
    auto icons = std::make_shared<Shape>();
    icons->graphics.beginFill(0);
    icons->graphics.drawRect(40, 40, 100, 20);
    icons->graphics.endFill();
    panel->addChild(button);
    panel->addChild(icons);
    stage->addChild(panel);
    // Over the button and the icon: the button.
    stage->pointerMove(60, 55);
    CHECK(stage->hovered() == button.get());
    // Over the icon alone: the sprite holding it.
    stage->pointerMove(120, 55);
    CHECK(stage->hovered() == panel.get());
    // A sprite that does not take the mouse leaves the icon to what is under.
    panel->mouseEnabled = false;
    stage->pointerMove(120, 55);
    CHECK(stage->hovered() == nullptr);
}

TEST(display_simple_button_shows_its_states)
{
    auto stage = std::make_shared<Stage>();
    auto button = std::make_shared<SimpleButton>();
    auto up = box(0, 0, 50, 20, 0x0000ff);
    auto over = box(0, 0, 50, 20, 0x00ff00);
    auto down = box(0, 0, 50, 20, 0xff0000);
    button->upState = up;
    button->overState = over;
    button->downState = down;
    button->hitTestState = up;
    stage->addChild(button);
    CHECK(button->shownState() == up.get());
    stage->pointerMove(10, 10);
    CHECK(button->shownState() == over.get());
    stage->pointerDown(10, 10);
    CHECK(button->shownState() == down.get());
    stage->pointerUp(10, 10);
    CHECK(button->shownState() == over.get());
    stage->pointerMove(100, 100);
    CHECK(button->shownState() == up.get());
    // A state swapped by a script, as bunnyPaint's tool buttons do.
    button->upState = button->downState;
    CHECK(button->shownState() == down.get());
    CHECK_NEAR(button->width(), 50.0);
}

TEST(display_text_fields_take_text_and_typing)
{
    auto stage = std::make_shared<Stage>();
    auto field = std::make_shared<TextField>();
    stage->addChild(field);
    field->setText("Hello\nworld");
    CHECK(field->text() == "Hello\rworld");
    CHECK(field->numLines() == 2);
    CHECK(field->getLineText(1) == "world");
    TextFormat bold;
    bold.bold = true;
    bold.color = 0xff0000;
    field->setTextFormat(bold, 0, 5);
    CHECK(field->getTextFormat(0, 5).bold.value_or(false));
    CHECK(!field->getTextFormat(0, 11).bold.has_value());
    const std::string html = field->htmlText();
    CHECK(html.find("<B>Hello</B>") != std::string::npos);
    CHECK(html.find("COLOR=\"#FF0000\"") != std::string::npos);
    // HTML in: tags make the formats.
    field->setHtmlText("<p align=\"center\"><b>Big</b> <font color=\"#00ff00\" size=\"20\">green</font></p><p>next</p>");
    CHECK(field->text() == "Big green\rnext");
    CHECK(field->getTextFormat(0, 3).bold.value_or(false));
    CHECK(field->getTextFormat(4, 9).color.value_or(0) == 0x00ff00u);
    CHECK(field->getTextFormat(4, 9).size.value_or(0) == 20.0);
    CHECK(field->getTextFormat(0, 1).align.value_or("") == "center");
    CHECK(field->getTextFormat(11, 12).align.value_or("") == "left");

    // Typing into an input field with the focus.
    auto input = std::make_shared<TextField>();
    input->setType(TextField::Type::Input);
    input->setY(100);
    stage->addChild(input);
    int changes = 0;
    input->addEventListener(events::kChange, [&](Event&) { ++changes; });
    stage->pointerDown(10, 110);
    stage->pointerUp(10, 110);
    CHECK(stage->focus() == input.get());
    for (char c : std::string("cat"))
        stage->keyDown(static_cast<uint32_t>(std::toupper(c)), static_cast<uint32_t>(c));
    CHECK(input->text() == "cat");
    CHECK(changes == 3);
    stage->keyDown(37, 0); // left
    stage->keyDown(8, 8);  // backspace
    CHECK(input->text() == "ct");
    CHECK(input->caretIndex() == 1);
    input->restrictTo = "0-9";
    stage->keyDown(65, 'a');
    stage->keyDown(49, '1');
    CHECK(input->text() == "c1t");
    // Clicking elsewhere takes the focus away.
    stage->pointerDown(300, 300);
    CHECK(stage->focus() == nullptr);

    // Scrolling a field too short for its lines.
    auto scroller = std::make_shared<TextField>();
    scroller->setFieldHeight(40);
    scroller->setText("1\n2\n3\n4\n5\n6\n7\n8");
    CHECK(scroller->numLines() == 8);
    CHECK(scroller->maxScrollV() > 1);
    scroller->setScrollV(100);
    CHECK(scroller->scrollV() == scroller->maxScrollV());
    CHECK(scroller->bottomScrollV() == 8);
    // autoSize grows the box to the text.
    auto sized = std::make_shared<TextField>();
    sized->setAutoSize(TextField::AutoSize::Left);
    sized->setText("wide wide wide wide wide wide");
    CHECK(sized->fieldWidth() > 100.0);
}

TEST(display_bitmap_data_channels_merges_and_compares)
{
    BitmapData a(4, 4, true, 0xff102030);
    BitmapData b(4, 4, true, 0xff405060);
    // Green into red.
    a.copyChannel(b, b.rect(), Point(), 2, 1);
    CHECK(a.getPixel32(0, 0) == 0xff502030u);
    // Half of each channel toward the other.
    BitmapData c(1, 1, true, 0xff000000);
    BitmapData white(1, 1, true, 0xffffffff);
    c.merge(white, white.rect(), Point(), 128, 128, 128, 128);
    CHECK(c.getPixel(0, 0) == 0x7f7f7fu);
    // A palette that inverts red.
    std::vector<uint32_t> red(256);
    for (uint32_t i = 0; i < 256; ++i)
        red[i] = (255 - i) << 16;
    BitmapData d(1, 1, false, 0x00102030);
    d.paletteMap(d, d.rect(), Point(), &red, nullptr, nullptr, nullptr);
    CHECK(d.getPixel(0, 0) == 0xef2030u);
    // compare: the same, a different size, or the differences.
    std::shared_ptr<BitmapData> differences;
    BitmapData e(4, 4, true, 0xff102030);
    BitmapData f(4, 4, true, 0xff102030);
    CHECK(e.compare(f, differences) == 0 && !differences);
    BitmapData g(3, 4, true, 0);
    CHECK(e.compare(g, differences) == -3);
    f.setPixel32(1, 1, 0xff102031);
    CHECK(e.compare(f, differences) == 1 && differences && differences->getPixel32(1, 1) == 0xff0000ffu);
    // Where a color is.
    BitmapData h(10, 10, true, 0);
    h.fillRect(Rect(2, 3, 4, 2), 0xffff0000);
    const Rect found = h.getColorBoundsRect(0xffffffff, 0xffff0000);
    CHECK(found.x == 2 && found.y == 3 && found.width == 4 && found.height == 2);
    // Noise is the same from the same seed.
    BitmapData n1(8, 8, false, 0), n2(8, 8, false, 0);
    n1.noise(42);
    n2.noise(42);
    std::shared_ptr<BitmapData> none;
    CHECK(n1.compare(n2, none) == 0);
    // Locked, changes wait to be shown.
    BitmapData locked(2, 2, true, 0);
    const uint64_t before = locked.revision();
    locked.lock();
    locked.setPixel32(0, 0, 0xffffffff);
    CHECK(locked.revision() == before);
    locked.unlock();
    CHECK(locked.revision() != before);
}

TEST(display_hit_areas_masks_and_button_hit_states)
{
    auto stage = std::make_shared<Stage>(400, 300);
    // A sprite found by its hit area, not its art.
    auto art = box(0, 0, 50, 50, 0xff0000);
    auto area = box(200, 0, 50, 50, 0x00ff00);
    stage->addChild(art);
    stage->addChild(area);
    art->setHitArea(area);
    area->mouseEnabled = false;
    CHECK(stage->hitTarget(10, 10) == nullptr);
    CHECK(stage->hitTarget(210, 10) == art.get());
    // A masked sprite is found only where its mask lets it show.
    auto masked = box(0, 100, 100, 100, 0x0000ff);
    auto mask = box(0, 100, 30, 100, 0);
    stage->addChild(masked);
    stage->addChild(mask);
    masked->setMask(mask);
    CHECK(stage->hitTarget(10, 150) == masked.get());
    CHECK(stage->hitTarget(60, 150) == nullptr);
    // A button found by its hit test state, wherever its up state is.
    auto button = std::make_shared<SimpleButton>();
    button->upState = box(0, 0, 20, 20, 0);
    button->hitTestState = box(100, 0, 20, 20, 0);
    button->setY(250);
    stage->addChild(button);
    CHECK(stage->hitTarget(10, 260) == nullptr);
    CHECK(stage->hitTarget(110, 260) == button.get());
    // An invisible object is not found; scrollRect cuts what is.
    button->setVisible(false);
    CHECK(stage->hitTarget(110, 260) == nullptr);
    auto scrolled = box(300, 0, 100, 100, 0);
    scrolled->setScrollRect(Rect(50, 0, 50, 50));
    stage->addChild(scrolled);
    CHECK(stage->hitTarget(310, 10) == scrolled.get());
    CHECK(stage->hitTarget(360, 10) == nullptr);
    // Focus moves with focusIn and focusOut.
    auto first = std::make_shared<TextField>();
    auto second = std::make_shared<TextField>();
    stage->addChild(first);
    stage->addChild(second);
    std::vector<std::string> heard;
    first->addEventListener(events::kFocusOut, [&](Event&) { heard.push_back("first out"); });
    second->addEventListener(events::kFocusIn, [&](Event& e) {
        heard.push_back("second in");
        CHECK(static_cast<FocusEvent&>(e).relatedObject == first.get());
    });
    stage->setFocus(first.get());
    stage->setFocus(second.get());
    CHECK(heard.size() == 2u && heard[0] == "first out" && heard[1] == "second in");
    // Keys go to the focus and bubble to the stage.
    int keys = 0;
    stage->addEventListener(events::kKeyDown, [&](Event& e) {
        if (e.eventPhase == Event::Phase::Bubbling && e.target == second.get())
            ++keys;
    });
    stage->keyDown(65, 'a');
    CHECK(keys == 1);
    // Taken off the stage, it loses the focus.
    stage->removeChild(second.get());
    CHECK(stage->focus() == nullptr);
}
