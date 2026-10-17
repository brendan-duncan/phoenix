#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/element.h"
#include "../src/data/fla_document.h"
#include "../src/edit/html5_export.h"
#include "../src/edit/publish_settings.h"
#include "../src/parser/fla_parser.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

const fs::path kData = fs::path(__FILE__).parent_path() / "data";

/// Art as the same small shape whatever it is, so every piece is one; a
/// sound for every name asked for.
class FakeArt : public fla::html5::ArtSource
{
public:
    int calls = 0;
    std::vector<std::string> soundNames;

    std::string art(const fla::Element& element) override
    {
        ++calls;
        (void)element;
        return "{\"o\":[{\"p\":\"M0 0L1 0L1 1Z\",\"f\":{\"c\":0,\"a\":1}}],\"b\":[0,0,1,1]}";
    }
    int bitmap(const fla::Bitmap&) override { return -1; }
    int sound(const std::string& name) override
    {
        soundNames.push_back(name);
        return static_cast<int>(soundNames.size()) - 1;
    }
    std::string images() override { return "[]"; }
    std::string sounds() override { return "[]"; }
};

std::unique_ptr<fla::FLADocument> read(const fs::path& path)
{
    FLAParser parser;
    return std::unique_ptr<fla::FLADocument>(parser.parse(path.string()));
}

bool has(const std::string& text, const std::string& part)
{
    const bool found = text.find(part) != std::string::npos;
    if (!found)
        std::printf("    not in the content: %s\n", part.c_str());
    return found;
}

} // namespace

TEST(html5_numbers_and_strings_are_written_compactly)
{
    CHECK(fla::html5::number(1.0) == "1");
    CHECK(fla::html5::number(-0.0001) == "0");
    CHECK(fla::html5::number(2.5) == "2.5");
    CHECK(fla::html5::number(1.23456, 2) == "1.23");
    CHECK(fla::html5::number(100.10, 3) == "100.1");
    CHECK(fla::html5::quoted("a\"b\\c\nd") == "\"a\\\"b\\\\c\\nd\"");
    // Safe inside a page's script element.
    CHECK(fla::html5::quoted("</script>") == "\"\\u003c/script>\"");
}

TEST(html5_content_walks_timelines_frame_by_frame)
{
    const std::unique_ptr<fla::FLADocument> file = read(kData / "html5" / "timelines");
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    FakeArt art;
    std::vector<std::string> notes;
    const std::string content = fla::html5::content(*file->document, art, fla::html5::Options(), &notes);
    CHECK(has(content, "\"width\":320,\"height\":240,\"frameRate\":12"));
    CHECK(has(content, "\"background\":3368601"));
    // The symbol exported for ActionScript comes first, then those the
    // scenes show; one neither shows nor exports is left out.
    CHECK(has(content, "{\"name\":\"Hat\",\"type\":\"mc\",\"linkage\":\"com.studio.Hat\""));
    CHECK(has(content, "{\"name\":\"Spinner\",\"type\":\"graphic\""));
    CHECK(content.find("\"Unused\"") == std::string::npos);
    // Five frames, each its own (the tween moves every frame), the label
    // but not the comment.
    CHECK(has(content, "\"root\":{\"n\":5,\"scenes\":[{\"name\":\"Scene 1\",\"at\":0,\"n\":5}],\"labels\":[[0,\"start\"]]"));
    CHECK(has(content, "\"map\":[0,1,2,3,4]}}"));
    // The clip on the bottom layer: its matrix, name, filter and blend.
    CHECK(has(content, "{\"k\":\"S0L5E0\",\"d\":65537,\"t\":\"s\",\"id\":0,\"m\":[2,0,0,2,50,60],\"n\":\"hat\","
                       "\"f\":[{\"t\":\"blur\",\"bx\":8,\"by\":4,\"q\":1}],\"bm\":\"multiply\"}"));
    // The masked graphic half way through its tween: moved, faded, its loop
    // two frames on, masked by the mask layer's art.
    CHECK(has(content, "{\"k\":\"S0L4E0\",\"d\":131073,\"t\":\"s\",\"id\":1,\"m\":[1,0,0,1,50,10],\"gf\":2,"
                       "\"ct\":[1,1,1,0.75,0,0,0,0],\"mb\":3}"));
    CHECK(has(content, "{\"k\":\"S0L3E0\",\"d\":196609,\"t\":\"a\",\"id\":0,\"mk\":3}"));
    // The hidden layer and the guide are not published.
    CHECK(content.find("\"hidden\"") == std::string::npos);
    CHECK(content.find("S0L1E0") == std::string::npos);
    CHECK(content.find("S0L2E0") == std::string::npos);
    // The same art once, however many places use it.
    CHECK(has(content, "\"art\":[\n{\"o\":[{\"p\":\"M0 0L1 0L1 1Z\",\"f\":{\"c\":0,\"a\":1}}],\"b\":[0,0,1,1]}],"));
    // The hat's dynamic text: a field by its instance name.
    CHECK(has(content, "\"texts\":[{\"x\":0,\"y\":0,\"w\":80,\"h\":20,\"ml\":1,\"wrap\":1,\"runs\":[{\"t\":\"Hat\","
                       "\"f\":\"Arial\",\"s\":14,\"c\":1122867,\"b\":1,\"a\":\"center\"}]}]"));
    CHECK(has(content, "\"t\":\"x\",\"id\":0,\"n\":\"label_txt\""));
    CHECK(notes.empty());
}

TEST(html5_content_carries_canvas_scripts)
{
    const std::unique_ptr<fla::FLADocument> file = read(kData / "canvas" / "canvas_game");
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    FakeArt art;
    fla::html5::Options options;
    options.javaScript = true;
    const std::string content = fla::html5::content(*file->document, art, options, nullptr);
    CHECK(has(content, "\"javaScript\":true"));
    CHECK(has(content, "\"js\":[[0,\"this.stop();\\nvar root = this;"));
    CHECK(has(content, "{\"name\":\"Ball\",\"type\":\"mc\",\"linkage\":\"Ball\",\"timeline\":{\"n\":2,\"js\":[[0,\"this.stop();\"]]"));
    CHECK(has(content, "\"n\":\"ball\""));
    CHECK(has(content, "\"n\":\"score_txt\""));
    // Without JavaScript, scripts stay out.
    const std::string plain = fla::html5::content(*file->document, art, fla::html5::Options(), nullptr);
    CHECK(plain.find("\"js\"") == std::string::npos);
}

TEST(html5_publish_settings_round_trip)
{
    fla::publish::Settings settings = fla::publish::read(std::string(), "movie");
    CHECK(settings.html5 && !settings.html);
    CHECK(settings.html5Fit);
    CHECK(fla::publish::fileName(settings, "html5", "movie") == "movie.html");
    // Beside the SWF's HTML page, the HTML5 page moves aside.
    settings.html = true;
    CHECK(fla::publish::fileName(settings, "html5", "movie") == "movie_html5.html");
    settings.html5 = true;
    settings.html5Fit = false;
    settings.html5Loop = false;
    const std::string xml = fla::publish::write(std::string(), settings, "movie");
    CHECK(xml.find("id=\"Phoenix/HTML5\"") != std::string::npos);
    const fla::publish::Settings back = fla::publish::read(xml, "movie");
    CHECK(back.html5);
    CHECK(!back.html5Fit);
    CHECK(!back.html5Loop);
    // With the HTML page off, HTML5's page has the plain name.
    settings.html = false;
    CHECK(fla::publish::fileName(settings, "html5", "movie") == "movie.html");
    // An HTML5 Canvas document keeps it in its Canvas format's group.
    const std::string canvasXml =
        "<flash_profiles><flash_profile name=\"Default\" current=\"true\"><PublishFormatProperties/>"
        "<PublishProperties id=\"JavaScript/HTML\" name=\"JavaScript/HTML\" enabled=\"true\">"
        "<Property name=\"loop\">false</Property></PublishProperties></flash_profile></flash_profiles>";
    const fla::publish::Settings canvas = fla::publish::read(canvasXml, "game");
    CHECK(canvas.html5);
    CHECK(!canvas.html5Loop);
    fla::publish::Settings off = canvas;
    off.html5 = false;
    const std::string written = fla::publish::write(canvasXml, off, "game");
    CHECK(written.find("Phoenix/HTML5") == std::string::npos);
    CHECK(!fla::publish::read(written, "game").html5);
}

namespace {

/// FakeArt that also draws pictures: one small one for any run of layers,
/// noting which run and frame.
class PictureArt : public FakeArt
{
public:
    std::vector<std::string> drawn;

    std::string picture(const fla::Timeline&, int from, int to, int frame) override
    {
        drawn.push_back(std::to_string(from) + "-" + std::to_string(to) + "@" + std::to_string(frame));
        return "{\"o\":[{\"i\":" + std::to_string(from) + ",\"m\":[1,0,0,1,0,0],\"b\":[0,0,1,1]}],\"b\":[0,0,1,1]}";
    }
};

} // namespace

TEST(html5_phoenix_effects_are_native_or_pictures)
{
    const std::unique_ptr<fla::FLADocument> file = read(kData / "html5" / "effects");
    CHECK(file && file->document && !file->document->timelines.empty());
    if (!file || !file->document || file->document->timelines.empty())
        return;
    // Layers from the top: Camera, Glow, Ball, Matte, Stripes, Wiggle,
    // Soft, Cards, Back. The effect layer goes with the layer under it
    // (which has motion blur too), the matte with its matted layer, the
    // ripple no filter carries alone; the blur a filter carries stays.
    const std::vector<fla::html5::BakedRun> runs = fla::html5::bakedRuns(*file->document->timelines.front());
    CHECK(runs.size() == 3);
    if (runs.size() == 3)
    {
        CHECK(runs[0].from == 1 && runs[0].to == 3);
        CHECK(runs[0].why.find("an effect layer") != std::string::npos);
        CHECK(runs[0].why.find("motion blur") != std::string::npos);
        CHECK(runs[1].from == 3 && runs[1].to == 5 && runs[1].why == "a track matte");
        CHECK(runs[2].from == 7 && runs[2].to == 8);
    }
    PictureArt art;
    std::vector<std::string> notes;
    const std::string content = fla::html5::content(*file->document, art, fla::html5::Options(), &notes);
    // A picture a frame for each run, in the place of its bottom layer.
    CHECK(art.drawn.size() == 3 * 24);
    CHECK(has(content, "{\"k\":\"S0B1\",\"d\":458752,\"t\":\"a\",\"id\":"));
    CHECK(has(content, "{\"k\":\"S0B7\",\"d\":131072,\"t\":\"a\",\"id\":"));
    CHECK(notes.size() == 3);
    CHECK(!notes.empty() && notes.front().find("Scene 1: layers Glow, Ball") == 0);
    // The blur on the soft card as its filter.
    CHECK(has(content, "\"k\":\"S0L6E0\""));
    CHECK(has(content, "\"f\":[{\"t\":\"blur\",\"bx\":6,\"by\":6,\"q\":3}]"));
    // The camera: at rest on frame 0, the back layer as drawn; its tint
    // over the whole picture, frame by frame.
    CHECK(has(content, "{\"k\":\"S0L8E0\",\"d\":65537,\"t\":\"a\",\"id\":0}"));
    CHECK(has(content, "\"cc\":[0,["));
    CHECK(has(content, "[0.65,0.65,0.65,1,89.25,47.6,0,0]]"));
}
