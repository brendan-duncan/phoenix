#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/onion_shift.h"
#include "../src/parser/fla_parser.h"
#include "../src/writer/xfl_folder_writer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool near(double a, double b)
{
    return std::abs(a - b) < 1.0e-9;
}

fla::Point mapped(const fla::Transform& m, double x, double y)
{
    return fla::Point(m.m11 * x + m.m21 * y + m.tx, m.m12 * x + m.m22 * y + m.ty);
}

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST(onion_shift_moves_turns_and_scales_a_ghost_about_its_middle)
{
    fla::onion::Shift shift;
    shift.pivot = fla::Point(100.0, 50.0);
    CHECK(!shift.moved());
    fla::Point at = mapped(fla::onion::matrix(shift), 130.0, 70.0);
    CHECK(near(at.x, 130.0) && near(at.y, 70.0));

    // Moved: everything goes the same way.
    shift.dx = 15.0;
    shift.dy = -5.0;
    CHECK(shift.moved());
    at = mapped(fla::onion::matrix(shift), 130.0, 70.0);
    CHECK(near(at.x, 145.0) && near(at.y, 65.0));

    // Turned a quarter clockwise and doubled: the middle goes only where it
    // was moved, a point to its right ends up below it, twice as far.
    shift.rotation = 90.0;
    shift.scale = 2.0;
    at = mapped(fla::onion::matrix(shift), 100.0, 50.0);
    CHECK(near(at.x, 115.0) && near(at.y, 45.0));
    at = mapped(fla::onion::matrix(shift), 110.0, 50.0);
    CHECK(near(at.x, 115.0) && near(at.y, 65.0));
}

TEST(onion_shifts_are_kept_by_frame_and_leave_the_document_alone)
{
    const fs::path folder = fs::temp_directory_path() / "phoenix_onion_shift_tests";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const std::string ns = "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"http://ns.adobe.com/xfl/2008/\"";
    std::ofstream(folder / "sample.xfl") << "PROXY-CS5";
    std::ofstream(folder / "DOMDocument.xml")
        << "<DOMDocument " + ns + " width=\"300\" height=\"200\" frameRate=\"24\"><timelines><DOMTimeline name=\"Scene 1\">"
           "<layers><DOMLayer name=\"Ink\"><frames>"
           "<DOMFrame index=\"0\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#000000\"/></FillStyle>"
           "</fills><edges><Edge fillStyle1=\"1\" edges=\"!0 0|400 0!400 0|400 400!400 400|0 0\"/></edges></DOMShape></elements></DOMFrame>"
           "<DOMFrame index=\"1\"><elements/></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>";
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse(folder.string()));
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    fla::XFLFolderWriter writer;
    CHECK(writer.write(document, (folder / "before").string()));

    // Ghosts of frame 1 of the scene shifted, turned and reset, the second
    // frame's shift its own.
    const fla::Timeline* scene = document.timelines.front();
    fla::onion::Shifts shifts;
    CHECK(shifts.empty() && !shifts.find(scene, 0));
    fla::onion::Shift& first = shifts.at(scene, 0, fla::Point(10.0, 10.0));
    first.dx = 40.0;
    first.rotation = 30.0;
    CHECK(shifts.at(scene, 0, fla::Point(99.0, 99.0)).pivot.x == 10.0);
    shifts.at(scene, 1, fla::Point(0.0, 0.0)).scale = 0.5;
    CHECK(shifts.size() == 2 && shifts.find(scene, 0)->dx == 40.0 && shifts.find(scene, 1)->dx == 0.0);
    CHECK(!shifts.find(&document, 0));

    // The document is written exactly as before: nothing of the shifts is in
    // it.
    CHECK(writer.write(document, (folder / "after").string()));
    const std::string before = readText(folder / "before" / "DOMDocument.xml");
    CHECK(!before.empty() && before == readText(folder / "after" / "DOMDocument.xml"));
    CHECK(document.timelines.front()->layers.front()->frames.size() == 2);

    shifts.reset();
    CHECK(shifts.empty() && !shifts.find(scene, 0));
    fs::remove_all(folder);
}
