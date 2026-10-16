#include "display_check.h"

#include "command_line.h"
#include "display_painter.h"
#include "phoenix_view.h"
#include "player.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../play/display/bitmap_data.h"
#include "../play/display/display_object.h"
#include "../play/display/stage.h"
#include "../play/display/text_field.h"
#include "../play/movie_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <memory>

using namespace fla::display;

namespace {

std::shared_ptr<Sprite> sprite(double x, double y)
{
    auto made = std::make_shared<Sprite>();
    made->setX(x);
    made->setY(y);
    return made;
}

std::shared_ptr<Sprite> box(double x, double y, double w, double h, uint32_t color, double alpha = 1.0)
{
    auto made = sprite(x, y);
    made->graphics.beginFill(color, alpha);
    made->graphics.drawRect(0, 0, w, h);
    made->graphics.endFill();
    return made;
}

std::shared_ptr<Sprite> disc(double x, double y, double r, uint32_t color, double alpha = 1.0)
{
    auto made = sprite(x, y);
    made->graphics.beginFill(color, alpha);
    made->graphics.drawCircle(0, 0, r);
    made->graphics.endFill();
    return made;
}

/// A label in plain text.
std::shared_ptr<TextField> label(double x, double y, const std::string& text)
{
    auto field = std::make_shared<TextField>();
    TextFormat format;
    format.font = "Arial";
    format.size = 11.0;
    field->setDefaultTextFormat(format);
    field->setAutoSize(TextField::AutoSize::Left);
    field->setText(text);
    field->setX(x);
    field->setY(y);
    return field;
}

Gradient gradientOf(bool radial, std::vector<uint32_t> colors, std::vector<double> alphas, std::vector<double> ratios,
    double w, double h, double rotation, double tx, double ty)
{
    Gradient g;
    g.radial = radial;
    g.stops = Gradient::stopsOf(colors, alphas, ratios);
    g.matrix.createGradientBox(w, h, rotation, tx, ty);
    return g;
}

/// The stage drawn as Test Movie draws it, onto a white image.
QImage drawStage(DisplayPainter& painter, const Stage& stage)
{
    QImage image(static_cast<int>(stage.stageWidth()), static_cast<int>(stage.stageHeight()),
        QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(255, 255, 255));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.stageToDevice = QTransform();
    painter.drawContent(p, stage);
    return image;
}

bool save(const QImage& image, const QString& folder, const char* name)
{
    const QString path = QDir(folder).filePath(name);
    if (image.isNull() || !image.save(path))
    {
        std::fprintf(stderr, "display-check: %s could not be written\n", qPrintable(path));
        return false;
    }
    std::printf("%s\n", qPrintable(QDir::toNativeSeparators(path)));
    return true;
}

/// Shapes, gradients, strokes, bitmaps, blend modes, filters, a mask, text
/// and a button.
QImage basics(DisplayPainter& painter)
{
    auto stage = std::make_shared<Stage>(640, 520);

    // Fills and strokes.
    auto shapes = sprite(0, 0);
    stage->addChild(shapes);
    Graphics& g = shapes->graphics;
    g.lineStyle(4, 0x000000, 1.0, false, LineStyle::ScaleMode::Normal, LineStyle::Caps::Square, LineStyle::Joints::Miter);
    g.beginFill(0xe03030);
    g.drawRect(20, 30, 90, 60);
    g.endFill();
    g.lineStyle();
    g.beginGradientFill(gradientOf(true, {0xffffff, 0x2050ff}, {1, 1}, {0, 255}, 90, 90, 0, 125, 15));
    g.drawCircle(170, 60, 45);
    g.endFill();
    g.lineStyle(2, 0x004000, 1.0, false, LineStyle::ScaleMode::Normal, LineStyle::Caps::Round, LineStyle::Joints::Round);
    g.beginGradientFill(gradientOf(false, {0xffcc00, 0x00aa44, 0x0044aa}, {1, 0.7, 1}, {0, 128, 255}, 120, 60, kPi / 4,
        230, 30));
    g.drawRoundRect(230, 30, 120, 60, 24);
    g.endFill();
    g.lineStyle(6, 0xff8800, 1.0, false, LineStyle::ScaleMode::Normal, LineStyle::Caps::None);
    g.moveTo(370, 90);
    g.curveTo(400, 10, 430, 90);
    g.cubicCurveTo(450, 20, 480, 120, 510, 40);
    g.lineStyle(0, 0x000000);
    for (int i = 0; i < 5; ++i)
    {
        g.moveTo(530, 30 + i * 15);
        g.lineTo(620, 30 + i * 15);
    }
    // Two rectangles in one fill: their overlap is a hole.
    g.lineStyle();
    g.beginFill(0x7030a0);
    g.drawRect(530, 20, 50, 40);
    g.drawRect(555, 45, 50, 40);
    g.endFill();
    stage->addChild(label(20, 100, "fills, gradients, caps, hairlines, even-odd"));

    // A bitmap made a pixel at a time, as it is, scaled smooth, and noise.
    auto pixels = std::make_shared<BitmapData>(64, 64, true, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            pixels->setPixel32(x, y, (static_cast<uint32_t>(255 - y * 3) << 24) | (static_cast<uint32_t>(x * 4) << 16) |
                    (static_cast<uint32_t>(y * 4) << 8) | 0x80);
    auto plain = std::make_shared<Bitmap>(pixels);
    plain->setX(20);
    plain->setY(130);
    stage->addChild(plain);
    auto scaled = std::make_shared<Bitmap>(pixels, PixelSnapping::Auto, true);
    scaled->setX(100);
    scaled->setY(130);
    scaled->setScaleX(2.0);
    scaled->setScaleY(1.0);
    scaled->setRotation(10);
    stage->addChild(scaled);
    auto noise = std::make_shared<BitmapData>(96, 64, false, 0);
    noise->perlinNoise(24, 24, 3, 7, false, true, 7, false);
    auto noisy = std::make_shared<Bitmap>(noise);
    noisy->setX(260);
    noisy->setY(130);
    stage->addChild(noisy);
    // BitmapData.draw of a sprite: a gradient spectrum drawn a line at a
    // time, as bunnyPaint's color picker draws it.
    auto spectrumSprite = std::make_shared<Sprite>();
    const int sw = 120, sh = 64;
    for (int i = 0; i < sw; ++i)
    {
        const double turn = -2.0 * kPi * i / sw;
        const uint32_t r = static_cast<uint32_t>(std::cos(turn) * 127 + 128);
        const uint32_t gg = static_cast<uint32_t>(std::cos(turn + 2 * kPi / 3) * 127 + 128);
        const uint32_t b = static_cast<uint32_t>(std::cos(turn + 4 * kPi / 3) * 127 + 128);
        const uint32_t color = (r << 16) | (gg << 8) | b;
        Graphics& s = spectrumSprite->graphics;
        s.lineStyle(1, 0, 1, false, LineStyle::ScaleMode::None, LineStyle::Caps::None);
        s.lineGradientStyle(gradientOf(false, {0xffffff, color}, {1, 1}, {0, 255}, 1, sh / 2.0, kPi / 2, 0, 0));
        s.moveTo(i, 0);
        s.lineTo(i, sh / 2.0);
        s.lineGradientStyle(gradientOf(false, {color, 0}, {1, 1}, {0, 255}, 1, sh / 2.0, kPi / 2, 0, sh / 2.0));
        s.moveTo(i, sh / 2.0);
        s.lineTo(i, sh);
    }
    auto spectrum = std::make_shared<BitmapData>(sw, sh, false, 0);
    spectrum->draw(*spectrumSprite);
    auto spectrumBitmap = std::make_shared<Bitmap>(spectrum);
    spectrumBitmap->setX(380);
    spectrumBitmap->setY(130);
    stage->addChild(spectrumBitmap);
    stage->addChild(label(20, 200, "bitmaps: setPixel32 with alpha, scaled smooth, perlinNoise, draw(sprite)"));

    // Blend modes over a band of colors.
    const char* modes[] = {"normal", "multiply", "screen", "lighten", "darken", "difference", "add", "subtract",
        "invert", "overlay", "hardlight"};
    auto band = sprite(20, 230);
    stage->addChild(band);
    band->graphics.beginFill(0xd02020);
    band->graphics.drawRect(0, 0, 560, 25);
    band->graphics.beginFill(0x20a040);
    band->graphics.drawRect(0, 25, 560, 25);
    band->graphics.endFill();
    for (int i = 0; i < 11; ++i)
    {
        auto over = disc(20 + 25 + i * 50, 255, 20, 0x3080ff);
        BlendMode mode;
        blendModeNamed(modes[i], mode);
        over->setBlendMode(mode);
        stage->addChild(over);
    }
    // A Layer group with an erase child cuts a hole in itself only.
    auto group = box(590, 230, 40, 50, 0xffaa00);
    group->setBlendMode(BlendMode::Layer);
    auto eraser = disc(20, 25, 12, 0);
    eraser->setBlendMode(BlendMode::Erase);
    group->addChild(eraser);
    stage->addChild(group);
    stage->addChild(label(20, 282, "blend modes: normal multiply screen lighten darken difference add subtract invert "
                                   "overlay hardlight, layer+erase"));

    // Filters.
    const auto star = [](double x, double y, uint32_t color) {
        auto made = sprite(x, y);
        made->graphics.beginFill(color);
        for (int i = 0; i <= 10; ++i)
        {
            const double r = i % 2 ? 14 : 32;
            const double a = -kPi / 2 + i * kPi / 5;
            if (i == 0)
                made->graphics.moveTo(r * std::cos(a), r * std::sin(a));
            else
                made->graphics.lineTo(r * std::cos(a), r * std::sin(a));
        }
        made->graphics.endFill();
        return made;
    };
    auto shadowed = star(60, 345, 0x3366cc);
    shadowed->setFilters({BitmapFilter::dropShadow(6, 45, 0, 0.6, 6, 6)});
    auto blurred = star(150, 345, 0x3366cc);
    blurred->setFilters({BitmapFilter::blur(8, 8, 2)});
    auto glowing = star(240, 345, 0x3366cc);
    glowing->setFilters({BitmapFilter::glow(0xffcc00, 1, 12, 12, 3)});
    auto beveled = star(330, 345, 0x3366cc);
    beveled->setFilters({BitmapFilter::bevel(4, 45, 0xffffff, 1, 0, 1, 4, 4, 2)});
    auto gray = sprite(420, 345);
    gray->graphics.beginGradientFill(gradientOf(false, {0xff0000, 0x00ff00, 0x0000ff}, {1, 1, 1}, {0, 128, 255}, 60, 60, 0, -30, -30));
    gray->graphics.drawCircle(0, 0, 30);
    gray->setFilters({BitmapFilter::colorMatrix({0.3, 0.59, 0.11, 0, 0, 0.3, 0.59, 0.11, 0, 0, 0.3, 0.59, 0.11, 0, 0, 0, 0, 0, 1, 0})});
    auto half = star(510, 345, 0x3366cc);
    half->setFilters({BitmapFilter::colorMatrix({1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.4, 0}),
        BitmapFilter::dropShadow(4, 45, 0, 1, 4, 4)});
    for (const auto& each : {shadowed, blurred, glowing, beveled, gray, half})
        stage->addChild(each);
    stage->addChild(label(20, 385, "filters: drop shadow, blur, glow, bevel, gray matrix, alpha matrix + shadow"));

    // A mask, a tint, alpha on a container, text, and a button.
    auto stripes = sprite(20, 410);
    for (int i = 0; i < 10; ++i)
    {
        stripes->graphics.beginFill(i % 2 ? 0x2266aa : 0xaadd33);
        stripes->graphics.drawRect(i * 10, 0, 10, 80);
    }
    stripes->graphics.endFill();
    auto hole = disc(70, 450, 35, 0);
    stage->addChild(stripes);
    stage->addChild(hole);
    stripes->setMask(hole);
    auto tinted = star(170, 450, 0x3366cc);
    ColorTransform tint;
    tint.setColor(0xcc2266);
    tint.redMultiplier = tint.greenMultiplier = tint.blueMultiplier = 0.5;
    tint.redOffset *= 0.5;
    tint.greenOffset *= 0.5;
    tint.blueOffset *= 0.5;
    tinted->setColorTransform(tint);
    stage->addChild(tinted);
    auto faded = sprite(220, 420);
    faded->addChild(box(0, 0, 40, 40, 0xff0000));
    faded->addChild(box(20, 20, 40, 40, 0x0000ff));
    faded->setAlpha(0.5);
    faded->setRotation(-8);
    stage->addChild(faded);
    auto text = std::make_shared<TextField>();
    text->setX(300);
    text->setY(410);
    text->setFieldWidth(190);
    text->setFieldHeight(60);
    text->setBorder(true);
    text->setBackground(true);
    text->setBackgroundColor(0xffffee);
    text->setMultiline(true);
    text->setWordWrap(true);
    text->setHtmlText("<font face=\"Arial\" size=\"14\"><b>Bold</b> <i>italic</i> <font color=\"#cc0000\" size=\"18\">red"
                      "</font> <u>under</u></font><p align=\"right\"><font face=\"Arial\">right aligned</font></p>");
    stage->addChild(text);
    auto input = std::make_shared<TextField>();
    input->setType(TextField::Type::Input);
    input->setX(300);
    input->setY(478);
    input->setFieldWidth(190);
    input->setFieldHeight(22);
    input->setBorder(true);
    TextFormat inputFormat;
    inputFormat.font = "Arial";
    inputFormat.size = 14;
    input->setDefaultTextFormat(inputFormat);
    stage->addChild(input);
    stage->setFocus(input.get());
    for (char c : std::string("typed here"))
        stage->keyDown(static_cast<uint32_t>(c), static_cast<uint32_t>(c));
    auto button = std::make_shared<SimpleButton>();
    button->upState = box(0, 0, 80, 30, 0x888888);
    button->overState = box(0, 0, 80, 30, 0x44aa44);
    button->downState = box(0, 0, 80, 30, 0x226622);
    button->hitTestState = button->upState;
    button->setX(520);
    button->setY(420);
    stage->addChild(button);
    stage->pointerMove(540, 430);
    stage->addChild(label(510, 455, "button (over)"));
    return drawStage(painter, *stage);
}

/// bunnyPaint's canvas: layers of bitmaps painted a pixel at a time, a
/// draw layer with filters, a mask, a drop shadow and an inverting cursor.
QImage canvas(DisplayPainter& painter, double& paintMs, double& drawMs)
{
    auto stage = std::make_shared<Stage>(520, 400);
    const int w = 400, h = 300;
    auto canvas = sprite(40, 40);
    stage->addChild(canvas);
    auto drawMask = box(0, 0, w, h, 0xff0000);
    canvas->addChild(drawMask);
    drawMask->setWidth(w + 40);
    drawMask->setHeight(h + 40);
    // At rest the mask reaches past the canvas, so its shadow shows.
    canvas->setMask(drawMask);
    auto background = box(0, 0, w, h, 0xffffff);
    canvas->addChild(background);

    // Two layers, each a bitmap in a sprite.
    std::vector<std::shared_ptr<BitmapData>> bitmaps;
    std::vector<std::shared_ptr<Sprite>> layers;
    for (int i = 0; i < 2; ++i)
    {
        auto layer = std::make_shared<Sprite>();
        auto data = std::make_shared<BitmapData>(w, h, true, 0x00ffffff);
        layer->addChild(std::make_shared<Bitmap>(data));
        canvas->addChild(layer);
        bitmaps.push_back(data);
        layers.push_back(layer);
    }
    // Brush dabs merged a pixel at a time, as BP_SimpleBrush merges its
    // opacity buffer: getPixel and getPixel32 read, setPixel32 written.
    QElapsedTimer timer;
    timer.start();
    BitmapData opacity(w, h, false, 0);
    const std::shared_ptr<BitmapData> original = bitmaps[0]->clone();
    long pixelsTouched = 0;
    for (int dab = 0; dab < 40; ++dab)
    {
        const double cx = 40 + dab * 8.0;
        const double cy = 150 + std::sin(dab * 0.3) * 80.0;
        for (int y = static_cast<int>(cy) - 30; y < static_cast<int>(cy) + 30; ++y)
            for (int x = static_cast<int>(cx) - 30; x < static_cast<int>(cx) + 30; ++x)
            {
                const double d = std::hypot(x - cx, y - cy) / 30.0;
                const uint32_t brush = d < 1.0 ? static_cast<uint32_t>((1.0 - d) * 255.0) * 255 : 0;
                if (brush > opacity.getPixel(x, y))
                    opacity.setPixel(x, y, brush);
                ++pixelsTouched;
            }
    }
    const uint32_t color = 0x69b8fe;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const uint32_t opacityAlpha = opacity.getPixel(x, y) / 255;
            ++pixelsTouched;
            if (opacityAlpha == 0)
                continue;
            const uint32_t dest = original->getPixel32(x, y);
            const uint32_t destAlpha = (dest >> 24) & 0xff;
            const uint32_t layerAlpha = opacityAlpha + destAlpha * (255 - opacityAlpha) / 255;
            const uint32_t real = 255 * opacityAlpha / std::max(1u, layerAlpha);
            const uint32_t inv = 255 - real;
            const uint32_t r = ((((color >> 16) & 0xff) * real + ((dest >> 16) & 0xff) * inv) / 255) << 16 & 0xff0000;
            const uint32_t g = ((((color >> 8) & 0xff) * real + ((dest >> 8) & 0xff) * inv) / 255) << 8 & 0xff00;
            const uint32_t b = (((color & 0xff) * real + (dest & 0xff) * inv) / 255) & 0xff;
            bitmaps[0]->setPixel32(x, y, (layerAlpha << 24) | r | g | b);
        }
    paintMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    std::printf("brush merge: %ld pixels read or written in %.1f ms\n", pixelsTouched, paintMs);

    // The second layer: a flood-filled ring, at half opacity by a color
    // matrix, multiplied over the first.
    BitmapData& top = *bitmaps[1];
    for (int a = 0; a < 720; ++a)
        for (int r = 60; r < 64; ++r)
        {
            const double t = a * kPi / 360.0;
            top.setPixel32(static_cast<int>(280 + r * std::cos(t)), static_cast<int>(120 + r * std::sin(t)), 0xff000000);
        }
    top.floodFill(280, 120, 0xffff6600);
    layers[1]->setFilters({BitmapFilter::colorMatrix({1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, 0})});
    layers[1]->setBlendMode(BlendMode::Multiply);

    // A draw layer with a soft brush line, then baked into the first layer
    // with BitmapData.draw (filters and all).
    auto drawLayer = std::make_shared<Sprite>();
    canvas->addChildAt(drawLayer, canvas->getChildIndex(layers[0].get()) + 1);
    drawLayer->graphics.lineStyle(14, 0x228833, 1.0);
    drawLayer->graphics.moveTo(40, 260);
    drawLayer->graphics.curveTo(200, 180, 360, 270);
    drawLayer->setFilters({BitmapFilter::blur(6, 6, 2)});
    timer.restart();
    bitmaps[0]->draw(*drawLayer);
    drawMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    std::printf("BitmapData.draw of a filtered sprite onto %dx%d: %.1f ms\n", w, h, drawMs);
    drawLayer->graphics.clear();
    drawLayer->graphics.lineStyle(4, 0xaa00aa);
    drawLayer->graphics.moveTo(40, 40);
    drawLayer->graphics.lineTo(140, 80);

    // The cursor: an ellipse inverting what is under it.
    auto cursor = std::make_shared<Sprite>();
    cursor->setBlendMode(BlendMode::Invert);
    cursor->graphics.lineStyle(1, 0x000000, 1.0);
    cursor->graphics.drawEllipse(200 - 20, 150 - 20, 40, 40);
    canvas->addChild(cursor);

    // The canvas's drop shadow.
    canvas->setFilters({BitmapFilter::dropShadow(10, 45, 0, 0.5, 8, 8, 0.45)});
    return drawStage(painter, *stage);
}

/// A movie as Test Movie draws it, with display objects put among its
/// timeline's: one under everything, one over, and a filter, a blend and
/// a hidden child on clips the timeline placed.
int movieCheck(const QString& file, const QString& folder, int frames)
{
    std::string error;
    std::unique_ptr<fla::FLADocument> document = commands::load(file.toStdString(), error);
    if (!document || !document->document || !document->document->currentScene())
    {
        std::fprintf(stderr, "%s\n", error.empty() ? "Could not open it." : error.c_str());
        return 1;
    }
    fla::Document& doc = *document->document;
    const std::vector<const fla::Timeline*> scenes(doc.timelines.begin(), doc.timelines.end());
    fla::play::MovieRuntime movie(scenes, doc.frameRate > 0.0 ? doc.frameRate : 24.0);
    movie.error = [](const std::string& text) { std::printf("script error: %s\n", text.c_str()); };
    std::vector<const fla::Symbol*> symbols;
    if (doc.symbolList)
        symbols.assign(doc.symbolList->symbols.begin(), doc.symbolList->symbols.end());
    movie.setLibrary(symbols, {}, {});
    movie.start();
    for (int i = 0; i < frames; ++i)
        movie.step();

    Player player;
    PhoenixView view(&player);
    view.setDocument(document.get());
    view.setShowPasteboard(false);
    view.setClipOutsideStage(true);
    view.setMovie(&movie);
    player.setCurrentFrame(movie.root().frame);
    const QSize size(std::max(1, static_cast<int>(std::lround(doc.width))), std::max(1, static_cast<int>(std::lround(doc.height))));
    bool ok = save(view.renderFrame(movie.root().frame, size, false), folder, "movie_before.png");

    // Under the timeline's art and over it; the clips it placed changed.
    auto* root = static_cast<DisplayObjectContainer*>(movie.root().display.get());
    std::printf("root display list: %d children\n", root->numChildren());
    for (int i = 0; i < root->numChildren() && i < 40; ++i)
    {
        const std::shared_ptr<DisplayObject> child = root->getChildAt(i);
        const char* kinds[] = {"Shape", "Sprite", "MovieClip", "SimpleButton", "Bitmap", "TextField", "Stage", "TimelineArt"};
        std::printf("  %d %s \"%s\" depth %d\n", i, kinds[static_cast<int>(child->kind())], child->name().c_str(),
            child->timeline ? child->timeline->depth : -1);
    }
    auto under = box(0, 0, doc.width, doc.height / 2, 0x3366ff);
    root->addChildAt(under, 0);
    auto over = disc(doc.width / 2, doc.height / 2, std::min(doc.width, doc.height) / 6, 0xff2200, 0.6);
    over->setFilters({BitmapFilter::glow(0xffff00, 1, 16, 16, 2)});
    root->addChild(over);
    int changed = 0;
    for (int i = 0; i < root->numChildren(); ++i)
    {
        const std::shared_ptr<DisplayObject> child = root->getChildAt(i);
        if (!child->clip || child == under || child == over)
            continue;
        if (changed == 0)
            child->setFilters({BitmapFilter::dropShadow(8, 45, 0, 0.8, 4, 4)});
        else if (changed == 1)
            child->setBlendMode(BlendMode::Invert);
        else if (changed == 2)
            child->setVisible(false);
        ++changed;
    }
    // The main timeline's own drawing, under all of it, as a document class
    // draws; and the timeline's art hidden, faded and turned.
    Graphics& rootDrawing = static_cast<MovieClip*>(root)->graphics;
    rootDrawing.lineStyle(8, 0x000000, 0.8);
    rootDrawing.beginFill(0xffee88, 0.5);
    rootDrawing.drawRoundRect(doc.width * 0.55, doc.height * 0.05, doc.width * 0.4, doc.height * 0.3, 40);
    rootDrawing.endFill();
    int art = 0;
    for (int i = 0; i < root->numChildren(); ++i)
    {
        const std::shared_ptr<DisplayObject> child = root->getChildAt(i);
        if (child->kind() != ObjectKind::TimelineArt)
            continue;
        if (art == 1)
            child->setVisible(false);
        else if (art == 2)
        {
            child->setAlpha(0.3);
            child->setRotation(5);
        }
        ++art;
    }
    // Under the first clip, a shape added at its index.
    for (int i = 0; i < root->numChildren(); ++i)
        if (root->getChildAt(i)->clip)
        {
            root->addChildAt(box(10, 10, doc.width / 3, doc.height / 3, 0x22cc44, 0.8), i);
            break;
        }
    ok = save(view.renderFrame(movie.root().frame, size, false), folder, "movie_after.png") && ok;

    // Reordered: the glowing disc to the bottom, under the timeline's art,
    // and the first button the timeline placed to the top.
    root->setChildIndex(over.get(), 0);
    for (int i = 0; i < root->numChildren(); ++i)
        if (root->getChildAt(i)->kind() == ObjectKind::SimpleButton)
        {
            root->setChildIndex(root->getChildAt(i).get(), root->numChildren() - 1);
            break;
        }
    ok = save(view.renderFrame(movie.root().frame, size, false), folder, "movie_reordered.png") && ok;

    // What the pointer finds, through the timeline's art.
    Stage& stage = movie.stage();
    for (const QPointF& at : {QPointF(65, 260), QPointF(65, 400), QPointF(560, 22), QPointF(300, 150)})
    {
        const InteractiveObject* hit = stage.hitTarget(at.x(), at.y());
        std::printf("under (%g, %g): %s \"%s\"\n", at.x(), at.y(), hit ? (hit->kind() == ObjectKind::SimpleButton ? "button" : "object") : "nothing",
            hit ? hit->name().c_str() : "");
    }
    // A click through the view, as the window gets it: the display list's
    // listeners hear it, down, up and click, on what is under the pointer.
    view.resize(size);
    std::vector<std::string> heard;
    for (const char* type : {events::kMouseDown, events::kMouseUp, events::kClick})
        stage.addEventListener(type, [&heard](Event& e) {
            heard.push_back(e.type + " " + static_cast<DisplayObject*>(e.target)->name());
        });
    const QPointF at = view.mapToWidget(QPointF(65, 260));
    for (const QEvent::Type type : {QEvent::MouseMove, QEvent::MouseButtonPress, QEvent::MouseButtonRelease})
    {
        QMouseEvent event(type, at, view.mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &event);
    }
    for (const std::string& line : heard)
        std::printf("heard %s\n", line.c_str());
    view.setMovie(nullptr);
    return ok ? 0 : 1;
}

} // namespace

int displayCheck(const QStringList& args)
{
    if (args.isEmpty())
    {
        std::fprintf(stderr, "display-check: which folder to write to?\n");
        return 1;
    }
    const QString folder = args.at(0);
    QDir().mkpath(folder);
    DisplayPainter painter;
    setBackend(&painter);
    painter.drawTimeline = nullptr;
    bool ok = save(basics(painter), folder, "display_basics.png");
    double paintMs = 0.0, drawMs = 0.0;
    ok = save(canvas(painter, paintMs, drawMs), folder, "display_canvas.png") && ok;
    setBackend(nullptr);
    const int fla = static_cast<int>(args.indexOf("--fla"));
    if (fla >= 0 && fla + 1 < args.size())
    {
        const int frames = args.contains("--frames") ? args.at(args.indexOf("--frames") + 1).toInt() : 1;
        ok = movieCheck(args.at(fla + 1), folder, frames) == 0 && ok;
    }
    return ok ? 0 : 1;
}
