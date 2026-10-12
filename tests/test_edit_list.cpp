#include "test_util.h"

#include "../src/edit/edit_list.h"

TEST(an_edl_and_a_final_cut_xml_read_as_shots_and_sounds)
{
    CHECK(fla::editlist::timecodeFrames("01:00:02:12", 24.0) == (3600 + 2) * 24 + 12);
    CHECK(fla::editlist::timecodeFrames("00:00:00:05", 24.0) == 5);
    CHECK(fla::editlist::timecodeFrames("nope", 24.0) == -1);

    const std::string edl =
        "TITLE: EP101 animatic\n"
        "FCM: NON-DROP FRAME\n"
        "\n"
        "001  SH010    V     C        00:00:00:00 00:00:02:00 01:00:00:00 01:00:02:00\n"
        "* FROM CLIP NAME: sh010.png\n"
        "002  SH020    V     C        00:00:00:00 00:00:01:12 01:00:02:00 01:00:03:12\n"
        "* FROM CLIP NAME: sh020.png\n"
        "003  AX       A     C        00:00:10:00 00:00:13:12 01:00:00:00 01:00:03:12\n"
        "* FROM CLIP NAME: dialog.wav\n";
    const fla::editlist::EditList list = fla::editlist::parse(edl, 24.0);
    CHECK(list.title == "EP101 animatic" && list.events.size() == 3);
    CHECK(list.events[0].start == 0 && list.events[0].end == 48 && fla::editlist::shotName(list.events[0]) == "sh010");
    CHECK(list.events[1].start == 48 && list.events[1].end == 84);
    CHECK(list.events[2].audio && list.events[2].sourceIn == 240 && list.events[2].name == "dialog.wav");

    const std::string xml =
        "<?xml version=\"1.0\"?><xmeml version=\"4\"><sequence><name>EP101</name>"
        "<rate><timebase>25</timebase><ntsc>FALSE</ntsc></rate><media>"
        "<video><track><clipitem><name>sh010</name><start>0</start><end>50</end><in>0</in>"
        "<file><pathurl>file://localhost/C%3A/boards/sh010.jpg</pathurl></file></clipitem>"
        "<clipitem><name>sh020</name><start>50</start><end>75</end><in>0</in></clipitem></track></video>"
        "<audio><track><clipitem><name>vo.wav</name><start>10</start><end>60</end><in>25</in></clipitem></track>"
        "<track><clipitem><name>music.wav</name><start>0</start><end>75</end><in>0</in></clipitem></track></audio>"
        "</media></sequence></xmeml>";
    const fla::editlist::EditList cut = fla::editlist::parse(xml, 24.0);
    CHECK(cut.frameRate == 25.0 && cut.events.size() == 4);
    CHECK(cut.events[0].file == "C:/boards/sh010.jpg");
    CHECK(cut.events[2].audio && cut.events[2].track == 1 && cut.events[2].start == 10 && cut.events[2].sourceIn == 25);
    CHECK(cut.events[3].track == 2);
}
