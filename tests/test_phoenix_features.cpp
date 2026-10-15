#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/edit/phoenix_features.h"
#include "../src/parser/fla_parser.h"
#include "../src/phx/phx_document.h"
#include "../src/writer/xfl_folder_writer.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

/// An XFL document using every Phoenix-only feature, each where Phoenix keeps
/// it: on the document, a symbol, layers, frames, an instance, a shape's fill
/// and stroke, a text field and its run.
void writeSample(const fs::path& folder)
{
    fs::remove_all(folder);
    writeText(folder / "DOMDocument.xml",
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"550\" height=\"400\" "
        "phoenixPalettes=\"Day~skin=FFCC99\" phoenixStageAlpha=\"0.5\" phoenixControllers=\"Smile\" "
        "phoenixPerspectiveAngle=\"60\" phoenixSafeZones=\"title=1,90;frames=4:3,1,00BEFFFF,c,0\" "
        "phoenixFutureThing=\"1\">"
        "<symbols><Include href=\"Peg.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"Camera\" layerType=\"guide\" phoenixCamera=\"true\"><frames>"
        "<DOMFrame index=\"0\" phoenixCameraX=\"275\" phoenixCameraY=\"200\"><elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"Notes\" layerType=\"guide\" phoenixReview=\"true\"><frames><DOMFrame index=\"0\"><elements>"
        "<DOMStaticText phoenixNote=\"true\" phoenixNoteAuthor=\"Sam\" phoenixNoteDone=\"false\"><textRuns><DOMTextRun>"
        "<characters>Fix the arm</characters><textAttrs><DOMTextAttrs face=\"Arial\" size=\"12\" phoenixColor=\"ink\"/>"
        "</textAttrs></DOMTextRun></textRuns></DOMStaticText></elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"Glow\" phoenixEffect=\"glow\" phoenixTransparency=\"50\" phoenixLineArt=\"Ink\" "
        "phoenixParentScale=\"false\"><frames>"
        "<DOMFrame index=\"0\" phoenixShapeHints=\"0,0,10,10\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Peg\" phoenixDeformer=\"bend\" phoenixBehaviors=\"wiggle\" phoenixRotationX=\"20\" phoenixZ=\"5\" "
        "phoenixPaint=\"1\" phoenixMinRotation=\"-30\" phoenixMaxRotation=\"30\" phoenixBinding=\"anchors\"/>"
        "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\" phoenixColor=\"skin\" "
        "phoenixTexture=\"Grain\" phoenixTextureScale=\"2\"/></FillStyle></fills>"
        "<strokes><StrokeStyle index=\"1\"><SolidStroke weight=\"2\" phoenixBrush=\"Rope\" phoenixBrushPattern=\"true\">"
        "<fill><SolidColor color=\"#000000\"/></fill></SolidStroke></StrokeStyle></strokes>"
        "<edges><Edge fillStyle1=\"1\" strokeStyle=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/>"
        "</edges></DOMShape></elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    writeText(folder / "LIBRARY" / "Peg.xml",
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Peg\" symbolType=\"graphic\" "
        "phoenixPeg=\"true\" phoenixMouthChart=\"A=1\"><timeline><DOMTimeline name=\"Peg\"><layers>"
        "<DOMLayer name=\"Layer 1\"><frames><DOMFrame index=\"0\"><elements/></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timeline></DOMSymbolItem>");
}

std::set<std::string> idsOf(const fla::Document& document)
{
    const std::vector<std::string> ids = fla::features::ids(fla::features::used(document));
    return std::set<std::string>(ids.begin(), ids.end());
}

} // namespace

TEST(every_phoenix_feature_is_found)
{
    const fs::path folder = fs::temp_directory_path() / "phoenix_feature_tests" / "sample";
    writeSample(folder);
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> document(parser.parse(folder.string()));
    CHECK(document && document->document);
    if (!document || !document->document)
        return;
    const std::set<std::string> found = idsOf(*document->document);
    for (const fla::features::Feature& feature : fla::features::all())
    {
        if (!found.count(feature.id))
            std::printf("    not found: %s\n", feature.id);
        CHECK(found.count(feature.id) == 1);
    }
    // An attribute of Phoenix's the list does not name still counts.
    CHECK(found.count("other-phoenix-settings") == 1);
}

TEST(phoenix_features_survive_xfl_and_phx)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_feature_tests";
    writeSample(root / "sample");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> original(parser.parse((root / "sample").string()));
    CHECK(original && original->document);
    if (!original || !original->document)
        return;
    const std::set<std::string> expected = idsOf(*original->document);

    // Through an XFL folder, as kept for version control.
    fs::remove_all(root / "xfl");
    fla::XFLFolderWriter writer;
    CHECK(writer.write(*original->document, (root / "xfl").string()));
    FLAParser again;
    std::unique_ptr<fla::FLADocument> fromXfl(again.parse((root / "xfl").string()));
    CHECK(fromXfl && fromXfl->document);
    if (fromXfl && fromXfl->document)
    {
        const std::set<std::string> found = idsOf(*fromXfl->document);
        for (const std::string& id : expected)
        {
            if (!found.count(id))
                std::printf("    lost in XFL: %s\n", id.c_str());
        }
        CHECK(found == expected);
    }

    // Through a Phoenix document.
    const std::string phxPath = (root / "sample.phx").string();
    std::string error;
    CHECK(phx::save(*original->document, phxPath, phx::SaveOptions(), error));
    std::string warning;
    bool readOnly = false;
    std::unique_ptr<fla::FLADocument> fromPhx(phx::load(phxPath, error, warning, readOnly));
    CHECK(fromPhx && fromPhx->document);
    if (fromPhx && fromPhx->document)
    {
        CHECK(idsOf(*fromPhx->document) == expected);
        // And the .phx says which it uses.
        const std::set<std::string> listed(fromPhx->document->phxFeatures.begin(), fromPhx->document->phxFeatures.end());
        CHECK(listed == expected);
    }
}
