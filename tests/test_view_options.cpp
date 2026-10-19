#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/new_document.h"
#include "../src/edit/phoenix_features.h"
#include "../src/edit/reference_board.h"
#include "../src/edit/stage_view.h"
#include "../src/parser/fla_parser.h"
#include "../src/phx/phx_document.h"
#include "../src/writer/fla_writer.h"
#include "../src/writer/xfl_folder_writer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sv = fla::stageview;
namespace refs = fla::references;

namespace {

bool near(double a, double b)
{
    return std::fabs(a - b) < 1.0e-6;
}

sv::Placement placement(bool mirrored, double rotation)
{
    sv::Placement p;
    p.viewWidth = 1000;
    p.viewHeight = 700;
    p.stageWidth = 550;
    p.stageHeight = 400;
    p.zoom = 1.5;
    p.panX = 30;
    p.panY = -12;
    p.rotation = rotation;
    p.pivotX = 100;
    p.pivotY = 80;
    p.mirrored = mirrored;
    return p;
}

std::filesystem::path scratchFolder()
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_view_options_tests";
    std::filesystem::create_directories(folder);
    return folder;
}

/// A board of two pictures, one of them locked and see-through, with the
/// separators the text uses in a name.
refs::Board sampleBoard(fla::Document& document, refs::Files& added)
{
    refs::Board board;
    refs::Image first;
    first.path = refs::newPath(document, "png");
    first.name = "model sheet, front|back";
    first.x = -320.5;
    first.y = 140.25;
    first.scale = 0.75;
    first.rotation = -12.5;
    first.opacity = 0.4;
    first.locked = true;
    added.emplace_back(first.path, fla::LazyBytes(std::vector<uint8_t>{0x89, 'P', 'N', 'G', 1, 2, 3}));
    refs::Image second;
    second.path = refs::newPath(document, "jpg", added);
    second.name = "color key";
    second.x = 900;
    second.y = 20;
    second.hidden = true;
    added.emplace_back(second.path, fla::LazyBytes(std::vector<uint8_t>{0xFF, 0xD8, 0xFF, 9, 8, 7}));
    board.images = {first, second};
    return board;
}

void checkBoardBack(const fla::Document& document, const refs::Board& board, const refs::Files& added)
{
    CHECK(refs::of(document) == board);
    for (const auto& [path, bytes] : added)
        CHECK(refs::bytesOf(document, path) == bytes.get());
}

} // namespace

TEST(mirror_view_maps_round_trip_and_flips_about_the_stage_middle)
{
    for (const bool mirrored : {false, true})
    {
        for (const double rotation : {0.0, 30.0, -135.0})
        {
            const sv::Affine toView = sv::stageToView(placement(mirrored, rotation));
            sv::Affine toStage;
            CHECK(toView.inverted(toStage));
            for (const auto& point : {std::pair<double, double>{0, 0}, {550, 400}, {-80, 255.5}, {1234, -77}})
            {
                double vx = 0, vy = 0, sx = 0, sy = 0;
                toView.map(point.first, point.second, vx, vy);
                toStage.map(vx, vy, sx, sy);
                CHECK(near(sx, point.first) && near(sy, point.second));
            }
        }
    }

    // Unturned, a mirrored stage puts each point where its mirror image
    // across the stage's middle was, and the middle stays put.
    const sv::Affine plain = sv::stageToView(placement(false, 0.0));
    const sv::Affine mirror = sv::stageToView(placement(true, 0.0));
    double ax = 0, ay = 0, bx = 0, by = 0;
    plain.map(550 - 40, 70, ax, ay);
    mirror.map(40, 70, bx, by);
    CHECK(near(ax, bx) && near(ay, by));
    plain.map(275, 200, ax, ay);
    mirror.map(275, 200, bx, by);
    CHECK(near(ax, bx) && near(ay, by));

    // Turned, the pivot stays where it was on screen, mirrored or not, and
    // the turn still goes clockwise on screen: a point to the right of the
    // pivot moves down.
    for (const bool mirrored : {false, true})
    {
        const sv::Affine straight = sv::stageToView(placement(mirrored, 0.0));
        const sv::Affine turned = sv::stageToView(placement(mirrored, 30.0));
        straight.map(100, 80, ax, ay);
        turned.map(100, 80, bx, by);
        CHECK(near(ax, bx) && near(ay, by));
        const double rightX = mirrored ? 0.0 : 200.0;
        double sx = 0, sy = 0, tx = 0, ty = 0;
        straight.map(rightX, 80, sx, sy);
        turned.map(rightX, 80, tx, ty);
        CHECK(sx > ax && ty > sy);
    }

    // Without a mirror or a turn, the mapping is the plain zoom and pan.
    CHECK(near(plain.a, 1.5) && near(plain.d, 1.5) && near(plain.b, 0) && near(plain.c, 0));
    CHECK(near(plain.tx, 30 + (1000 - 550 * 1.5) / 2) && near(plain.ty, -12 + (700 - 400 * 1.5) / 2));
}

TEST(grayscale_view_shows_each_color_at_its_brightness)
{
    CHECK(sv::luminance(0, 0, 0) == 0);
    CHECK(sv::luminance(255, 255, 255) == 255);
    CHECK(sv::luminance(255, 0, 0) == 76);
    CHECK(sv::luminance(0, 255, 0) == 150);
    CHECK(sv::luminance(0, 0, 255) == 29);
    // Green reads lighter than red, and red lighter than blue.
    CHECK(sv::luminance(0, 200, 0) > sv::luminance(200, 0, 0));
    CHECK(sv::luminance(200, 0, 0) > sv::luminance(0, 0, 200));

    std::vector<uint32_t> pixels = {0xFFFF0000u, 0x80402010u, 0x00000000u, 0xFF808080u};
    sv::grayscale(pixels.data(), pixels.size());
    CHECK(pixels[0] == 0xFF4C4C4Cu);
    const uint32_t gray = sv::luminance(0x40, 0x20, 0x10);
    CHECK(pixels[1] == (0x80000000u | gray << 16 | gray << 8 | gray));
    CHECK(pixels[2] == 0u);
    CHECK(pixels[3] == 0xFF808080u);
}

TEST(reference_board_text_round_trips_and_stays_out_of_the_art)
{
    std::unique_ptr<fla::FLADocument> made(fla::createEmptyDocument(640, 480, 24.0));
    fla::Document& document = *made->document;
    CHECK(refs::of(document).images.empty());
    const auto attributes = document.extraAttributes;
    const auto files = document.extraFiles.size();

    refs::Files added;
    const refs::Board board = sampleBoard(document, added);
    CHECK(refs::fromText(refs::toText(board)) == board);
    CHECK(board.images[0].path != board.images[1].path);

    // Put in undoably: the attribute and both files come and go together.
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new refs::SetBoardCommand(document, board, added, "Add Reference Image")));
    checkBoardBack(document, board, added);
    CHECK(document.extraFiles.size() == files + 2);
    // Listed among Phoenix's own features, as something other apps drop.
    const std::vector<std::string> ids = fla::features::ids(fla::features::used(document));
    CHECK(std::find(ids.begin(), ids.end(), "reference-board") != ids.end());
    // Nothing of it is in the timeline.
    CHECK(document.timelines.size() == 1 && document.timelines[0]->layers.size() == 1);

    // Removing a picture takes its file out of the package too.
    refs::Board fewer = board;
    fewer.images.erase(fewer.images.begin());
    stack.push(fla::CommandPtr(new refs::SetBoardCommand(document, fewer, {}, "Remove Reference Image")));
    CHECK(refs::of(document) == fewer);
    CHECK(refs::bytesOf(document, board.images[0].path).empty());
    stack.undo();
    checkBoardBack(document, board, added);
    stack.undo();
    CHECK(document.extraAttributes == attributes && document.extraFiles.size() == files);
    CHECK(refs::of(document).images.empty());
}

TEST(reference_board_saves_with_fla_xfl_and_phx)
{
    std::unique_ptr<fla::FLADocument> made(fla::createEmptyDocument(640, 480, 24.0));
    fla::Document& document = *made->document;
    refs::Files added;
    const refs::Board board = sampleBoard(document, added);
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new refs::SetBoardCommand(document, board, added, "Add Reference Image")));

    const std::filesystem::path folder = scratchFolder();
    const std::string fla = (folder / "board.fla").string();
    fla::FLAWriter writer;
    CHECK(writer.write(document, fla));
    fla::XFLFolderWriter folderWriter;
    const std::string xfl = (folder / "board_xfl").string();
    std::filesystem::remove_all(xfl);
    CHECK(folderWriter.write(document, xfl, "board"));
    for (const std::string& path : {fla, xfl})
    {
        FLAParser parser;
        std::unique_ptr<fla::FLADocument> back(parser.parse(path));
        CHECK(back && back->document);
        if (back && back->document)
            checkBoardBack(*back->document, board, added);
    }

    const std::string phx = (folder / "board.phx").string();
    std::string error, warning;
    bool readOnly = false;
    CHECK(phx::save(document, phx, phx::SaveOptions(), error));
    std::unique_ptr<fla::FLADocument> opened(phx::load(phx, error, warning, readOnly));
    CHECK(opened && opened->document);
    if (opened && opened->document)
    {
        checkBoardBack(*opened->document, board, added);
        // And back out to an FLA from the .phx.
        const std::string again = (folder / "board_again.fla").string();
        CHECK(writer.write(*opened->document, again));
        FLAParser parser;
        std::unique_ptr<fla::FLADocument> back(parser.parse(again));
        CHECK(back && back->document);
        if (back && back->document)
            checkBoardBack(*back->document, board, added);
    }
}
