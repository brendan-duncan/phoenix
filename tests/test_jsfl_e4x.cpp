// E4X in JSFL scripts: the source rewrite and the XML and XMLList it runs
// on, through a host with no documents.
#include "test_util.h"

#include "../src/jsfl/e4x.h"
#include "../src/jsfl/engine.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

class PlainHost : public jsfl::Host
{
public:
    std::string output;

    void trace(const std::string& text) override { output += text + "\n"; }
    void alert(const std::string& message) override { output += "alert: " + message + "\n"; }
    bool confirm(const std::string&) override { return true; }
    bool prompt(const std::string&, const std::string& initial, std::string& answer) override
    {
        answer = initial;
        return true;
    }
    std::string configDirectory() override
    {
        return (std::filesystem::temp_directory_path() / "phoenix_jsfl_test_config").u8string();
    }
    std::vector<fla::FLADocument*> documents() override { return {}; }
    fla::FLADocument* activeDocument() override { return nullptr; }
    void activate(fla::FLADocument*) override {}
    fla::FLADocument* open(const std::string&, std::string& error) override
    {
        error = "no documents here";
        return nullptr;
    }
    fla::FLADocument* create(std::string& error) override
    {
        error = "no documents here";
        return nullptr;
    }
    bool save(fla::FLADocument*, const std::string&, bool, std::string&) override { return false; }
    bool close(fla::FLADocument*, bool) override { return false; }
    std::string path(fla::FLADocument*) override { return {}; }
    std::string name(fla::FLADocument*) override { return {}; }
    bool modified(fla::FLADocument*) override { return false; }
    fla::CommandStack& history(fla::FLADocument*) override { throw std::runtime_error("no documents"); }
    fla::Selection& selection(fla::FLADocument*) override { throw std::runtime_error("no documents"); }
    fla::Timeline* editedTimeline(fla::FLADocument*) override { return nullptr; }
    void editTimeline(fla::FLADocument*, fla::Timeline*) override {}
    int currentFrame(fla::FLADocument*) override { return 0; }
    void setCurrentFrame(fla::FLADocument*, int) override {}
    int currentLayer(fla::FLADocument*) override { return 0; }
    void setCurrentLayer(fla::FLADocument*, int) override {}
    jsfl::Rect bounds(fla::FLADocument*, const fla::Element*) override { return {}; }
    bool exportPNG(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "no documents here";
        return false;
    }
};

/// The script's output, or its error.
std::string run(const std::string& source)
{
    PlainHost host;
    jsfl::Engine engine(host);
    jsfl::RunOptions options;
    options.timeLimit = 20.0;
    const jsfl::RunResult result = engine.runString(source, "e4x.jsfl", options);
    if (!result.ok)
        return host.output + "error: " + result.report() + "\n";
    return host.output;
}

#define CHECK_RUN(source, expected)                                                                \
    do                                                                                             \
    {                                                                                              \
        const std::string got = run(source);                                                       \
        const std::string wanted = (expected);                                                     \
        CHECK(got == wanted);                                                                      \
        if (got != wanted)                                                                         \
            std::printf("    got:\n%s    wanted:\n%s", got.c_str(), wanted.c_str());               \
    } while (0)

#define CHECK_REWRITE(source, expected)                                                            \
    do                                                                                             \
    {                                                                                              \
        const std::string got = jsfl::e4x::rewrite(source);                                        \
        const std::string wanted = (expected);                                                     \
        CHECK(got == wanted);                                                                      \
        if (got != wanted)                                                                         \
            std::printf("    got:\n%s\n    wanted:\n%s\n", got.c_str(), wanted.c_str());           \
    } while (0)

const char* const kDoc = "var x = new XML('<root a=\"1\" b=\"two\"><item id=\"i1\" n=\"3\">first</item>"
                         "<item id=\"i2\" n=\"5\">second</item><group><item id=\"i3\" n=\"7\"/></group></root>');\n";

} // namespace

TEST(jsfl_e4x_rewrite)
{
    // What JavaScript cannot parse becomes property names and calls.
    CHECK_REWRITE("a = x.@id;", "a = x[\"@?id\"];");
    CHECK_REWRITE("x.@id = 3;", "x[\"@?id\"] = 3;");
    CHECK_REWRITE("n = x.@id.length();", "n = x[\"@id\"].length();");
    CHECK_REWRITE("a = x.@*;", "a = x[\"@*\"];");
    CHECK_REWRITE("a = x.@[k + 1];", "a = x[\"@?\" + (k + 1)];");
    CHECK_REWRITE("a = x..item.@name;", "a = x[\"..item\"][\"@?name\"];");
    CHECK_REWRITE("a = x..*; b = x.*;", "a = x[\"..*\"]; b = x[\"*\"];");
    CHECK_REWRITE("a = x.item.(@id == 2);", "a = x.item[__e4x.filter]((__e4xIt) => { with (__e4xIt) return (__e4xIt[\"@?id\"] == 2); });");
    CHECK_REWRITE("x.* += y;", "x[__e4x.append] = y;");
    CHECK_REWRITE("XML.prototype.function::find = f;", "XML.prototype.find = f;");
    CHECK_REWRITE("a = <a b=\"c\">{d}</a>;", "a = XML(\"<a b=\\\"c\\\">\" + __e4x.hole(d, 0) + \"</a>\");");
    CHECK_REWRITE("a = <a b={c}/>;", "a = XML(\"<a b=\" + __e4x.hole(c, 1) + \"/>\");");
    CHECK_REWRITE("a = <>\n<b/></>;", "a = XMLList(\"\\n\" +\n\"<b/>\");");
    // Old Mac line ends end a comment too.
    CHECK_REWRITE("// c\ra = x.@id;", "// c\ra = x[\"@?id\"];");
    // Strings, regular expressions, comments, numbers and spread are left
    // alone, as are comparisons.
    const std::string plain = "var s = 'x.@id <a>' + \"..\"; // x.@id\n/* <b>{c}</b> */ r = /x.@y/g; n = 1..toString() + .5;"
                              " f(...args); if (a < b && c <d) {} q = a / b;";
    CHECK_REWRITE(plain, plain);
    CHECK(!jsfl::e4x::mayUse("var a = 1; if (a < b) fl.trace(a);"));
    CHECK(jsfl::e4x::mayUse("var a = <a/>;"));
    CHECK(jsfl::e4x::mayUse("var a = x.@id;"));

    // Literals nested past all reason are left as written, not followed
    // down until the stack runs out; nor are a few that never close tried
    // over and over through the rest. Shallower ones are still rewritten.
    for (const char* each : {"<a>{", "<>{", "x.(", "x.@["})
    {
        std::string deep = "a = ";
        for (int i = 0; i < 20000; ++i)
            deep += each;
        CHECK(!jsfl::e4x::rewrite(deep).empty());
    }
    std::string unclosed = "a = ";
    for (int i = 0; i < 30; ++i)
        unclosed += "<a>{";
    for (int i = 0; i < 5000; ++i)
        unclosed += "b < c; ";
    CHECK(!jsfl::e4x::rewrite(unclosed).empty());
    CHECK_REWRITE("a = <a>{<b>{<c/>}</b>}</a>;", "a = XML(\"<a>\" + __e4x.hole(XML(\"<b>\" + __e4x.hole(XML(\"<c/>\"), 0) + "
                                                "\"</b>\"), 0) + \"</a>\");");
}

TEST(jsfl_e4x_attributes)
{
    CHECK_RUN(std::string(kDoc) + "fl.trace(x.@a + ' ' + x.@b + ' ' + (x.@a == 1) + ' ' + (x.item[0].@id == x.item[0].@id));\n"
                                  "fl.trace(x.@missing == undefined);\n"
                                  "fl.trace(x.@a.length() + ' ' + x.@missing.length() + ' ' + x.@b.toString().toUpperCase());\n"
                                  "x.@a = 9; x.@c = 'new'; x.item[1].@n = 6; delete x.@b;\n"
                                  "fl.trace(x.@a + x.@c + x.item[1].@n + x.@b);\n"
                                  "var names = []; for each (var at in x.@*) names.push(at.name() + '=' + at); fl.trace(names.join(','));\n"
                                  "var key = 'id'; fl.trace(x.item[0].@[key]);\n"
                                  "fl.trace(x.item.@id + ' ' + x.attributes().length() + ' ' + x.attribute('a'));\n"
                                  "fl.trace(x.item[0].@id.split('i').join('-'));\n",
        "1 two true true\ntrue\n1 0 TWO\n9new6undefined\na=9,c=new\ni1\ni1i2 2 9\n-1\n");
}

TEST(jsfl_e4x_children_and_lists)
{
    CHECK_RUN(std::string(kDoc) + "fl.trace(x.item.length() + ' ' + x.item[1] + ' ' + x.item[1].@id + ' ' + x.children().length());\n"
                                  "fl.trace(x.*.length() + ' ' + x..item.length() + ' ' + x..@id + ' ' + x..item.@n);\n"
                                  "fl.trace(x.item.(@n > 3).@id + ' ' + x..item.(@id == 'i3').length());\n"
                                  "var big = x..item.(function::attribute('n') > 4); fl.trace(big.length());\n"
                                  "fl.trace(x.group.item.@id + ' ' + x.name() + ' ' + x.item[0].localName() + ' ' + x.item[0].text());\n"
                                  "fl.trace(x.item[0].parent().name() + ' ' + x.item[1].childIndex() + ' ' + x.item[0].nodeKind());\n"
                                  "var ids = []; for each (var it in x.item) ids.push(it.@id); fl.trace(ids.join());\n"
                                  "ids = []; for (var k in x.item) ids.push(k); fl.trace(ids.join());\n"
                                  "ids = []; for (var it of x..item) ids.push(it.@id); fl.trace(ids.join());\n"
                                  "fl.trace(x.nothing.length() + ' ' + (x.item[5] === undefined) + ' ' + (x.item[0] === x.item[0]));\n"
                                  "fl.trace(x.child('group').item.@id + ' ' + x.elements().length() + ' ' + x.descendants('item').length());\n"
                                  "fl.trace(x.hasOwnProperty('@a') + ' ' + x.hasOwnProperty('group') + ' ' + x.hasOwnProperty('@zz'));\n"
                                  "fl.trace(x.item[0].hasSimpleContent() + ' ' + x.hasComplexContent());\n",
        "2 second i2 3\n3 3 i1i2i3 357\ni2 1\n2\ni3 root item first\nroot 1 element\ni1,i2\n0,1\ni1,i2,i3\n"
        "0 true true\ni3 3 3\ntrue true false\ntrue true\n");
}

TEST(jsfl_e4x_filters_see_children)
{
    CHECK_RUN("var x = <list><p><name>a</name><age>3</age></p><p><name>b</name><age>9</age></p></list>;\n"
              "var limit = 5;\n"
              "fl.trace(x.p.(age > limit).name + ' ' + x.p.(name == 'a').age);\n"
              "fl.trace(x.p.(name.length() > 0 && age < 4).length());\n",
        "b 3\n1\n");
}

TEST(jsfl_e4x_literals_and_changes)
{
    CHECK_RUN("var id = 'b\"<'; var label = 'x & y';\n"
              "var d = <dialog title=\"T\">\n  <label value={label} id={id}/>\n  <text>{label}</text>\n</dialog>;\n"
              "fl.trace(d.toXMLString());\n"
              "fl.trace(d.label.@value + '|' + d.label.@id + '|' + d.text);\n"
              "d.appendChild(<button id=\"ok\"/>); d.prependChild(new XML('<first/>'));\n"
              "d.* += <last/>;\n"
              "fl.trace(d.children().length() + ' ' + d.*[0].name() + ' ' + d.*[4].name() + ' ' + d.button.@id);\n"
              "var list = <><a/><b/></>; fl.trace(list.length() + ' ' + typeof XMLList);\n"
              "d.text = 'changed'; d.extra = 'made'; fl.trace(d.text + d.extra);\n"
              "delete d.label; fl.trace(d.label.length());\n"
              "var e = <empty/>; e.inner.@k = 1; fl.trace(e.toXMLString());\n"
              "var T = 'tag', A = 'key'; var h = <{T} {A} = {1 + 1}><b/></{T}>; fl.trace(h.@key + h.name());\n"
              "XML.prettyPrinting = false; fl.trace(<a><b>1</b></a>.toXMLString());\n",
        "<dialog title=\"T\">\n  <label value=\"x &amp; y\" id=\"b&quot;&lt;\"/>\n  <text>x &amp; y</text>\n</dialog>\n"
        "x & y|b\"<|x & y\n"
        "5 first last ok\n"
        "2 function\n"
        "changedmade\n"
        "0\n"
        "<empty>\n  <inner k=\"1\"/>\n</empty>\n"
        "2tag\n"
        "<a><b>1</b></a>\n");
}

TEST(jsfl_e4x_parsing_and_strings)
{
    CHECK_RUN("var x = XML('<?xml version=\"1.0\"?>\\n<!DOCTYPE r>\\n<r><!-- c --><v>a &amp; b &#65;</v><![CDATA[<raw>]]></r>');\n"
              "fl.trace(x.v + '|' + x.text() + '|' + x.v.toString() + '|' + x.v.toXMLString());\n"
              "fl.trace(String(new XML('<a>t</a>')) + ' ' + XML('plain') + ' ' + (XML('<a>4</a>') * 2));\n"
              "fl.trace(new XMLList('<i/><i/><i/>').length() + ' ' + new XMLList().length());\n"
              "var y = x.copy(); y.v = 'z'; fl.trace(x.v + y.v + (x instanceof XML) + (x.v instanceof XMLList));\n"
              "fl.trace(x);\n"
              "try { XML('<a><b></a>'); } catch (e) { fl.trace(e.name); }\n",
        "a & b A|<raw>|a & b A|<v>a &amp; b A</v>\n"
        "t plain 8\n"
        "3 0\n"
        "a & b Aztruetrue\n"
        "<r>\n  <v>a &amp; b A</v>\n  &lt;raw&gt;\n</r>\n"
        "SyntaxError\n");
}

TEST(jsfl_e4x_flump_motion_xml)
{
    // Flump's motionXMLToJSON (MIT), the shape of its E4X: for each over
    // lists, attributes as strings, descendants.
    CHECK_RUN("var motionXMLToJSON = (function () {\n"
              "  var idToProperty = { Motion_X : 'x', Rotation_Z : 'rotation' };\n"
              "  var writeTimeMaps = function (timeMapArray, timeMapsXMLList) {\n"
              "    for each (var timeMapXML in timeMapsXMLList) {\n"
              "      timeMapArray.push({strength : parseInt(timeMapXML.@strength, 10), type : timeMapXML.@type.toString()});\n"
              "    }\n"
              "  }\n"
              "  var splitAttribute = function (attr) { return attr.split(',').map(function (v) { return parseFloat(v); }); }\n"
              "  var writePoints = function (pointsArray, keyframeXMLList, totalDuration) {\n"
              "    for each(var keyFrameXML in keyframeXMLList) {\n"
              "      var timeValue = parseInt(keyFrameXML.@timevalue, 10);\n"
              "      var prev = splitAttribute(keyFrameXML.@previous);\n"
              "      pointsArray.push({x : (prev[0] + timeValue) / totalDuration, y : prev[1]});\n"
              "    }\n"
              "  }\n"
              "  var writeProperties = function (propertyArray, propertyXMLList, totalDuration) {\n"
              "    for each(var propertyXML in propertyXMLList) {\n"
              "      var propName = idToProperty[propertyXML.@id.toString()];\n"
              "      if(!propName) continue;\n"
              "      var propJSON = {name : propName, points : []};\n"
              "      if(propertyXML.hasOwnProperty('@TimeMapIndex')) propJSON.timeMap = parseInt(propertyXML.@TimeMapIndex, 10);\n"
              "      writePoints(propJSON.points, propertyXML.Keyframe, totalDuration);\n"
              "      propertyArray.push(propJSON);\n"
              "    }\n"
              "  }\n"
              "  return function (frame) {\n"
              "    var xml = new XML(frame.getMotionObjectXML());\n"
              "    var json = { timeMaps : [], properties : [] };\n"
              "    var duration = parseInt(xml.@duration, 10) - 1000;\n"
              "    writeTimeMaps(json.timeMaps, xml.TimeMap);\n"
              "    writeProperties(json.properties, xml..Property, duration);\n"
              "    return json;\n"
              "  }\n"
              "})();\n"
              "var frame = { getMotionObjectXML: function () { return '<Motion duration=\"3000\"><TimeMap strength=\"0\" type=\"Quadratic\"/>"
              "<PropertyContainer id=\"headContainer\"><PropertyContainer id=\"Basic_Motion\">"
              "<Property id=\"Motion_X\" TimeMapIndex=\"0\"><Keyframe timevalue=\"0\" previous=\"0,0\"/>"
              "<Keyframe timevalue=\"1000\" previous=\"0,5\"/></Property><Property id=\"Other\"/>"
              "</PropertyContainer></PropertyContainer></Motion>'; } };\n"
              "fl.trace(JSON.stringify(motionXMLToJSON(frame)));\n",
        "{\"timeMaps\":[{\"strength\":0,\"type\":\"Quadratic\"}],\"properties\":[{\"name\":\"x\",\"points\":[{\"x\":0,\"y\":0},"
        "{\"x\":0.5,\"y\":5}],\"timeMap\":0}]}\n");
}

TEST(jsfl_e4x_corpus_scripts)
{
    // Opt in: PHOENIX_JSFL_CORPUS names a folder of .jsfl files; the ones
    // that use E4X are compiled and their count reported.
    const char* folder = std::getenv("PHOENIX_JSFL_CORPUS");
    if (!folder || !*folder)
    {
        std::printf("    skipped: set PHOENIX_JSFL_CORPUS to a folder of .jsfl files\n");
        return;
    }
    PlainHost host;
    jsfl::Engine engine(host);
    jsfl::RunOptions options;
    options.compileOnly = true;
    int total = 0;
    int failed = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(folder)))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsfl")
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const std::string source = text.str();
        if (jsfl::e4x::rewrite(source) == source)
            continue;
        ++total;
        const jsfl::RunResult result = engine.runFile(entry.path().u8string(), options);
        if (!result.ok)
        {
            ++failed;
            std::printf("    %s\n", result.report().c_str());
        }
    }
    std::printf("    %d scripts use E4X syntax, %d do not compile\n", total, failed);
}
