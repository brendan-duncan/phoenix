#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/edit/vector_import.h"

#include <cmath>
#include <cstdint>

namespace {

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

TEST(svg_paths_shapes_and_styles_are_read)
{
    const std::string svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 100 50">
  <g transform="translate(10,5)" fill="#f00">
    <rect x="0" y="0" width="10" height="20"/>
    <circle cx="30" cy="10" r="5" style="fill:rgb(0,0,255);stroke:black;stroke-width:2"/>
  </g>
  <path d="M0 0 h10 v10 z m20 0 l5 5" fill="none" stroke="#0f0"/>
  <line x1="0" y1="0" x2="4" y2="0" stroke="blue"/>
  <polygon points="0,0 4,0 4,4" fill="none"/>
  <text x="5" y="20" font-family="'Helvetica', sans-serif" font-size="12" font-weight="bold">Hi  there</text>
</svg>)svg";
    const fla::vectors::Artwork art = fla::vectors::readSvg(svg);
    CHECK(near(art.width, 200.0) && near(art.height, 100.0));
    // The polygon paints nothing: three paths remain, then the line.
    CHECK(art.paths.size() == 4);
    CHECK(art.notes.empty());
    // The text, its spaces run together, its baseline placed by the view box.
    CHECK(art.texts.size() == 1);
    if (art.texts.size() != 1)
        return;
    CHECK(art.texts[0].text == "Hi there" && art.texts[0].family == "Helvetica" && art.texts[0].bold);
    CHECK(near(art.texts[0].size, 12.0) && near(art.texts[0].placement.tx, 10.0) && near(art.texts[0].placement.ty, 40.0));
    CHECK(art.order.size() == 5 && !art.order.empty() && art.order.back().first == fla::vectors::Artwork::Kind::Text);

    // The rect: the group's translate, then the view box's doubling.
    const fla::vectors::Painted& rect = art.paths[0];
    CHECK(rect.fill && (*rect.fill)[0] == 255 && (*rect.fill)[2] == 0 && !rect.stroke);
    CHECK(near(rect.subpaths[0].segments[0].points[0].x, 20.0) && near(rect.subpaths[0].segments[0].points[0].y, 10.0));
    CHECK(near(rect.subpaths[0].segments[2].points[0].x, 40.0) && near(rect.subpaths[0].segments[2].points[0].y, 50.0));

    const fla::vectors::Painted& circle = art.paths[1];
    CHECK(circle.fill && (*circle.fill)[2] == 255 && circle.stroke && near(circle.strokeWidth, 4.0));
    CHECK(circle.subpaths[0].closed && circle.subpaths[0].segments.size() == 5);

    // Two subpaths; the relative move starts from the closed one's start.
    const fla::vectors::Painted& path = art.paths[2];
    CHECK(!path.fill && path.stroke && (*path.stroke)[1] == 255);
    CHECK(path.subpaths.size() == 2 && path.subpaths[0].closed && !path.subpaths[1].closed);
    CHECK(near(path.subpaths[1].segments[0].points[0].x, 40.0) && near(path.subpaths[1].segments[1].points[0].x, 50.0));

    CHECK(!art.paths[3].fill && art.paths[3].stroke);
}

TEST(svg_arcs_land_on_their_end_and_stay_on_the_circle)
{
    const fla::Point from(10.0, 0.0);
    const fla::Point to(-10.0, 0.0);
    const std::vector<fla::vectors::Segment> half = fla::vectors::arcToCubics(from, 10.0, 10.0, 0.0, false, true, to);
    CHECK(half.size() == 2);
    CHECK(near(half.back().points[2].x, -10.0) && near(half.back().points[2].y, 0.0));
    // Sweeping from (10,0) toward positive angles: through (0,10).
    CHECK(near(half[0].points[2].x, 0.0, 1.0e-6) && near(half[0].points[2].y, 10.0, 1.0e-6));

    // Radii too small to reach are scaled up to fit.
    const std::vector<fla::vectors::Segment> scaled = fla::vectors::arcToCubics(from, 1.0, 1.0, 0.0, false, false, to);
    CHECK(!scaled.empty() && near(scaled.back().points[2].x, -10.0));
    CHECK(near(scaled[0].points[2].y, -10.0, 1.0e-6));
}

TEST(pdf_page_paths_are_read_upright)
{
    const std::string content = "q 1 0 0 1 10 0 cm 1 0 0 rg 0 0 50 20 re f Q 0 0 1 RG 3 w 0 100 m 50 100 l S";
    std::string pdf = "%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                      "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                      "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 4 0 R >> endobj\n"
                      "4 0 obj << /Length " +
        std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream\nendobj\n%%EOF\n";
    const fla::vectors::Artwork art = fla::vectors::readPdf(std::vector<uint8_t>(pdf.begin(), pdf.end()));
    CHECK(near(art.width, 200.0) && near(art.height, 100.0));
    CHECK(art.paths.size() == 2);
    const fla::vectors::Painted& rect = art.paths[0];
    CHECK(rect.fill && (*rect.fill)[0] == 255 && !rect.stroke);
    // The page's bottom left is the stage's (0, 100).
    CHECK(near(rect.subpaths[0].segments[0].points[0].x, 10.0) && near(rect.subpaths[0].segments[0].points[0].y, 100.0));
    CHECK(near(rect.subpaths[0].segments[2].points[0].y, 80.0));
    const fla::vectors::Painted& line = art.paths[1];
    CHECK(!line.fill && line.stroke && (*line.stroke)[2] == 255 && near(line.strokeWidth, 3.0));
    CHECK(near(line.subpaths[0].segments[0].points[0].y, 0.0));

    const fla::vectors::Artwork none = fla::vectors::readPdf(std::vector<uint8_t>{'x', 'y'});
    CHECK(none.paths.empty() && !none.notes.empty());
}

TEST(painted_paths_become_drawing_objects_with_holes)
{
    fla::vectors::Painted ring;
    const auto square = [](double l, double t, double r, double b, bool clockwise) {
        fla::vectors::Subpath s;
        s.closed = true;
        s.segments.push_back({fla::vectors::Segment::Kind::Move, {fla::Point(l, t)}});
        if (clockwise)
            s.segments.insert(s.segments.end(), {{fla::vectors::Segment::Kind::Line, {fla::Point(r, t)}},
                {fla::vectors::Segment::Kind::Line, {fla::Point(r, b)}}, {fla::vectors::Segment::Kind::Line, {fla::Point(l, b)}}});
        else
            s.segments.insert(s.segments.end(), {{fla::vectors::Segment::Kind::Line, {fla::Point(l, b)}},
                {fla::vectors::Segment::Kind::Line, {fla::Point(r, b)}}, {fla::vectors::Segment::Kind::Line, {fla::Point(r, t)}}});
        return s;
    };
    // Both wound the same way: the inner one is still a hole.
    ring.subpaths = {square(0, 0, 100, 100, true), square(25, 25, 75, 75, true)};
    ring.fill = fla::vectors::Color{10, 20, 30, 255};
    ring.stroke = fla::vectors::Color{0, 0, 0, 255};
    ring.strokeWidth = 2.0;
    fla::Shape* shape = fla::vectors::toShape(ring);
    CHECK(shape != nullptr);
    CHECK(shape->isDrawingObject && shape->edges.size() == 2);
    CHECK(shape->fillsMap.count(1) == 1 && shape->strokesMap.count(1) == 1);
    const fla::Edge* outer = shape->edges[0];
    const fla::Edge* inner = shape->edges[1];
    // Opposite sides: the fill is between the two.
    CHECK((outer->fillStyle1 == 1) == (inner->fillStyle0 == 1));
    CHECK(outer->strokeStyle == 1 && inner->strokeStyle == 1);
    // Closed with a line back to the start.
    CHECK(outer->paths[0]->segments.size() == 5);
    CHECK(near(shape->localBounds.bottomRight.x, 100.0) && near(shape->localBounds.bottomRight.y, 100.0));
    delete shape;

    fla::vectors::Painted nothing;
    nothing.subpaths = {square(0, 0, 1, 1, true)};
    CHECK(fla::vectors::toShape(nothing) == nullptr);
}

TEST(svg_gradients_classes_use_and_images_are_read)
{
    const std::string svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="100" height="100">
  <defs>
    <style>.warm { fill: #ff8000 } .thin, line { stroke-width: 0.5 }</style>
    <linearGradient id="base"><stop offset="0" stop-color="#000"/><stop offset="100%" style="stop-color:#fff;stop-opacity:0.5"/></linearGradient>
    <linearGradient id="across" xlink:href="#base" x1="0" y1="0" x2="1" y2="0" spreadMethod="reflect"/>
    <radialGradient id="glow" gradientUnits="userSpaceOnUse" cx="50" cy="50" r="20"><stop offset="0" stop-color="red"/><stop offset="1" stop-color="blue"/></radialGradient>
    <rect id="tile" width="4" height="4" class="warm"/>
  </defs>
  <rect x="10" y="10" width="40" height="20" fill="url(#across)"/>
  <circle cx="50" cy="50" r="20" fill="url( #glow )"/>
  <use xlink:href="#tile" x="70" y="80"/>
  <image x="0" y="90" width="8" height="4" xlink:href="data:image/png;base64,iVBORw0KGgo="/>
</svg>)svg";
    const fla::vectors::Artwork art = fla::vectors::readSvg(svg);
    CHECK(art.paths.size() == 3 && art.images.size() == 1);
    if (art.paths.size() != 3 || art.images.size() != 1 || art.order.size() != 4)
        return;

    // The linear one takes its stops from the gradient it names, and spans
    // the rect's width: its square's x axis runs 40 pixels.
    const fla::vectors::Painted& rect = art.paths[0];
    CHECK(rect.gradient && !rect.gradient->radial && rect.gradient->stops.size() == 2);
    CHECK(rect.gradient->stops[1].color[0] == 255 && rect.gradient->stops[1].color[3] == 128);
    CHECK(rect.gradient->spread == fla::vectors::Gradient::Spread::Reflect);
    CHECK(near(rect.gradient->placement.a * 1638.4, 40.0, 1.0e-6) && near(rect.gradient->placement.tx, 30.0));
    CHECK(rect.fill && (*rect.fill)[0] == 0);

    // The radial one in user space: centered, 20 pixels out.
    const fla::vectors::Painted& circle = art.paths[1];
    CHECK(circle.gradient && circle.gradient->radial);
    CHECK(near(circle.gradient->placement.tx, 50.0) && near(circle.gradient->placement.a * 819.2, 20.0, 1.0e-6));

    // The used rect, placed, with its class's fill.
    const fla::vectors::Painted& tile = art.paths[2];
    CHECK(tile.fill && (*tile.fill)[0] == 255 && (*tile.fill)[1] == 128);
    CHECK(near(tile.subpaths[0].segments[0].points[0].x, 70.0) && near(tile.subpaths[0].segments[0].points[0].y, 80.0));

    // The image's bytes, decoded, and its box.
    const fla::vectors::Image& image = art.images[0];
    CHECK(image.encoded.size() == 8 && image.encoded[1] == 'P' && image.encoded[2] == 'N');
    CHECK(near(image.placement.a, 8.0) && near(image.placement.d, 4.0) && near(image.placement.ty, 90.0));
    CHECK(art.order[3].first == fla::vectors::Artwork::Kind::Image);

    // Shapes take the gradient as an FLA fill.
    fla::Shape* shape = fla::vectors::toShape(rect);
    CHECK(shape && shape->fills.size() == 1 && shape->fills[0]->type() == fla::FillStyle::Type::LinearGradient);
    CHECK(shape->fills[0]->spreadMethod == "reflect");
    delete shape;
}

TEST(pdf_forms_shadings_and_opacity_are_read)
{
    // A form drawn twice, moved; a gradient painted through a clip; and a
    // half-opaque fill.
    const std::string form = "0 0 1 rg 0 0 10 10 re f";
    const std::string content =
        "q 1 0 0 1 20 0 cm /Fm0 Do Q q 1 0 0 1 40 0 cm /Fm0 Do Q "
        "q 0 50 100 20 re W n /Sh0 sh Q "
        "/GS0 gs 1 0 0 rg 0 80 10 10 re f";
    std::string pdf = "%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                      "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                      "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 4 0 R "
                      "/Resources << /XObject << /Fm0 5 0 R >> /Shading << /Sh0 6 0 R >> /ExtGState << /GS0 << /ca 0.5 >> >> >> >> endobj\n"
                      "4 0 obj << /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream\nendobj\n"
                      "5 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 10 10] /Length " + std::to_string(form.size()) +
        " >>\nstream\n" + form + "\nendstream\nendobj\n"
        "6 0 obj << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 100 0] /Function 7 0 R >> endobj\n"
        "7 0 obj << /FunctionType 3 /Domain [0 1] /Bounds [0.5] /Encode [0 1 0 1] /Functions [8 0 R 9 0 R] >> endobj\n"
        "8 0 obj << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 1 0] /N 1 >> endobj\n"
        "9 0 obj << /FunctionType 2 /Domain [0 1] /C0 [0 1 0] /C1 [0 0 1] /N 1 >> endobj\n%%EOF\n";
    const fla::vectors::Artwork art = fla::vectors::readPdf(std::vector<uint8_t>(pdf.begin(), pdf.end()));
    CHECK(art.paths.size() == 4);
    if (art.paths.size() != 4)
        return;
    // The form, twice, where each put it.
    CHECK(near(art.paths[0].subpaths[0].segments[0].points[0].x, 20.0));
    CHECK(near(art.paths[1].subpaths[0].segments[0].points[0].x, 40.0));
    CHECK(art.paths[0].fill && (*art.paths[0].fill)[2] == 255);
    // The shading: the clip's rectangle, filled red to green to blue.
    const fla::vectors::Painted& shaded = art.paths[2];
    CHECK(shaded.gradient && shaded.gradient->stops.size() == 3);
    CHECK(shaded.gradient->stops[0].color[0] == 255 && shaded.gradient->stops[1].color[1] == 255);
    CHECK(near(shaded.gradient->stops[1].offset, 0.5) && near(shaded.gradient->placement.a * 1638.4, 100.0, 1.0e-6));
    // Half opaque.
    CHECK(art.paths[3].fill && (*art.paths[3].fill)[3] == 128);
    CHECK(art.notes.empty());
}

TEST(pdf_import_reads_text_through_its_fonts)
{
    // A simple font (its encoding's differences and widths) and a composite
    // one read through its ToUnicode map, turned a quarter.
    const std::string content = "BT /F1 10 Tf 1 0 0 1 10 80 Tm (AB\\222) Tj (A) Tj ET "
                                "BT 0 0 1 rg [(A) -500 (A)] TJ ET "
                                "BT /F2 20 Tf 0 1 -1 0 50 30 Tm <000100020003> Tj ET "
                                "BT /F1 10 Tf 3 Tr (hidden) Tj ET";
    const std::string cmap = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
                             "1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
                             "1 beginbfchar <0001> <0048> endbfchar\n"
                             "1 beginbfrange <0002> <0003> <0069> endbfrange\n"
                             "endcmap end end";
    std::string pdf = "%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                      "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                      "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 4 0 R "
                      "/Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> >> endobj\n"
                      "4 0 obj << /Length " + std::to_string(content.size()) + " >>\nstream\n" + content +
        "\nendstream\nendobj\n"
        "5 0 obj << /Type /Font /Subtype /TrueType /BaseFont /ABCDEF+ArialMT /FirstChar 65 /Widths [600 700] "
        "/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [66 /eacute] >> >> endobj\n"
        "6 0 obj << /Type /Font /Subtype /Type0 /BaseFont /TimesNewRomanPS-BoldMT /Encoding /Identity-H "
        "/DescendantFonts [7 0 R] /ToUnicode 8 0 R >> endobj\n"
        "7 0 obj << /Type /Font /Subtype /CIDFontType2 /DW 1000 /W [1 [500]] >> endobj\n"
        "8 0 obj << /Length " + std::to_string(cmap.size()) + " >>\nstream\n" + cmap + "\nendstream\nendobj\n%%EOF\n";
    const fla::vectors::Artwork art = fla::vectors::readPdf(std::vector<uint8_t>(pdf.begin(), pdf.end()));
    CHECK(art.texts.size() == 4);
    if (art.texts.size() != 4)
        return;
    const fla::vectors::Text& first = art.texts[0];
    CHECK(first.text == "A\xC3\xA9\xE2\x80\x99"); // A, e acute, right quote
    CHECK(first.family == "Arial" && !first.bold && near(first.size, 10.0));
    CHECK(near(first.placement.tx, 10.0) && near(first.placement.ty, 20.0));
    // On by the widths: 600 and 700 thousandths, then the missing width.
    CHECK(near(art.texts[1].placement.tx, 28.0));
    // A wide gap in a TJ array is a space; the fill color is the text's.
    CHECK(art.texts[2].text == "A A" && art.texts[2].color[2] == 255);
    const fla::vectors::Text& turned = art.texts[3];
    CHECK(turned.text == "Hij");
    CHECK(turned.family == "Times New Roman" && turned.bold && near(turned.size, 20.0));
    CHECK(near(turned.placement.a, 0.0) && near(turned.placement.b, -1.0));
    CHECK(near(turned.placement.tx, 50.0) && near(turned.placement.ty, 70.0));
}
