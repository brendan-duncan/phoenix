#include "test_util.h"

#include "../src/data/document.h"
#include "../src/edit/font_items.h"
#include "../src/writer/xfl_writer.h"

TEST(font_symbols_read_write_and_sit_before_the_media)
{
    fla::Document document(nullptr);
    document.extraSections.push_back({"<somethingElse/>", true});
    CHECK(fla::fonts::itemsOf(document).empty());

    fla::fonts::FontItem title;
    title.name = "Title";
    title.font = "Arial-BoldMT";
    title.ranges = {1, 2, 3};
    title.characters = "&!";
    fla::fonts::FontItem body;
    body.name = "Body";
    body.font = "TimesNewRomanPSMT";
    document.extraSections = fla::fonts::withItems(document, {title, body});
    CHECK(document.extraSections.size() == 2 && document.extraSections[0].xml == "<somethingElse/>");

    const std::vector<fla::fonts::FontItem> read = fla::fonts::itemsOf(document);
    CHECK(read.size() == 2 && read[0].name == "Title" && read[0].font == "Arial-BoldMT");
    CHECK((read[0].ranges == std::vector<int>{1, 2, 3}) && read[0].characters == "&!");
    CHECK(read[0].id != read[1].id && read[1].ranges.empty());

    CHECK(fla::fonts::isSymbol("Title*") && !fla::fonts::isSymbol("ArialMT"));
    CHECK(fla::fonts::fontOf(document, "Title*") == "Arial-BoldMT");
    CHECK(fla::fonts::fontOf(document, "ArialMT") == "ArialMT");
    CHECK(fla::fonts::fontOf(document, "Gone*") == "Gone*");

    // Written between the folders and the media, other sections after.
    fla::XFLWriter writer;
    const std::string xml = writer.writeDocument(document);
    const size_t fonts = xml.find("<fonts>");
    const size_t other = xml.find("<somethingElse");
    const size_t timelines = xml.find("<timelines");
    CHECK(fonts != std::string::npos && other != std::string::npos && fonts < other && other < timelines);
    CHECK(xml.find("<fonts>", fonts + 1) == std::string::npos);

    // None left: the section goes.
    document.extraSections = fla::fonts::withItems(document, {});
    CHECK(document.extraSections.size() == 1);
}
