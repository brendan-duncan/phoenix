// Phoenix's own paint brushes: original art drawn for Phoenix and
// shipped with it, so the brush library has brushes to draw with before
// any are added. Brushes from other applications' libraries are not
// shipped with Phoenix; a person adds those to their own brush library
// from a document that has them (Import Brushes from Document).

#include "brush_library.h"

namespace fla {

namespace {

struct OwnBrush
{
    const char* name;
    bool pattern;
    const char* guid;
    const char* svgId;
    /// The art, in grays so a stroke's color tints it: an art brush's from
    /// end to end, a pattern brush's one tile.
    const char* svg;
    /// For an art brush stretched between guides (scaleOptionm 2), the
    /// guides as percentages; else stretched whole.
    const char* leftGuide = nullptr;
    const char* rightGuide = nullptr;
};

const OwnBrush kOwnBrushes[] = {
    {"Tapered Stroke", false, "C46C6478-314C-4182-8CDA-194E518FF857", "72E3FA51-E476-4D71-8BF5-3611AA1A71B3",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 20">)~"
        R"~(<path d="M0,10 C40,3 120,0 200,10 C120,20 40,17 0,10 Z"/></svg>)~"},
    {"Ribbon", false, "C9F53E4D-E3D7-4148-B3A6-35F3CE2631F1", "91EE0FBB-9768-4F0A-A200-149E7FC76718",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 24">)~"
        R"~(<polygon points="0,2 200,2 190,12 200,22 0,22 10,12"/>)~"
        R"~(<polygon style="fill:#8C8C8C;" points="16,10 184,10 184,14 16,14"/></svg>)~",
        "8.00", "92.00"},
    {"Arrow", false, "9923203F-6949-4321-80BF-54E5DE8AD321", "C571B991-AFB0-4D22-B0FD-72BB244516C3",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 30">)~"
        R"~(<rect x="0" y="12" width="160" height="6"/><polygon points="155,0 200,15 155,30 162,15"/></svg>)~",
        "0.00", "77.50"},
    {"Dash Dot", false, "714F1297-B90E-42E2-A261-55CE8830C666", "512C93F3-BA4F-4D80-8A54-669CE1BB95AA",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 10">)~"
        R"~(<rect x="0" y="0" width="60" height="10" rx="5"/><circle cx="80" cy="5" r="5"/>)~"
        R"~(<rect x="100" y="0" width="60" height="10" rx="5"/><circle cx="180" cy="5" r="5"/></svg>)~"},
    {"Vine", false, "89B79DB5-E282-47AF-AFB2-F9A0718113DE", "49F650B5-CF56-45DF-A6D8-927592B8F910",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 40">)~"
        R"~(<path style="fill:none;stroke:#000000;stroke-width:2.5;" )~"
        R"~(d="M0,20 C25,8 50,8 75,20 S125,32 150,20 S190,10 200,20"/>)~"
        R"~(<ellipse cx="40" cy="7" rx="9" ry="4" transform="rotate(-30 40 7)"/>)~"
        R"~(<ellipse cx="62" cy="6" rx="8" ry="3.5" transform="rotate(25 62 6)"/>)~"
        R"~(<ellipse cx="108" cy="33" rx="9" ry="4" transform="rotate(30 108 33)"/>)~"
        R"~(<ellipse cx="132" cy="34" rx="8" ry="3.5" transform="rotate(-25 132 34)"/>)~"
        R"~(<ellipse cx="178" cy="8" rx="9" ry="4" transform="rotate(-30 178 8)"/></svg>)~"},
    {"Rope", false, "077FD645-2D1C-45A8-B1D0-AA46ACA3B1EC", "9B255B13-B73B-460D-AE53-A9657D8EE044",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 20">)~"
        R"~(<rect style="fill:#9A9A9A;" x="0" y="2" width="200" height="16"/>)~"
        R"~(<path d="M-2,20 C3,13 11,5 18,0 L24,0 C17,6 9,14 4,20 Z M20,20 C25,13 33,5 40,0 L46,0 C39,6 31,14 26,20 Z )~"
        R"~(M42,20 C47,13 55,5 62,0 L68,0 C61,6 53,14 48,20 Z M64,20 C69,13 77,5 84,0 L90,0 C83,6 75,14 70,20 Z )~"
        R"~(M86,20 C91,13 99,5 106,0 L112,0 C105,6 97,14 92,20 Z M108,20 C113,13 121,5 128,0 L134,0 C127,6 119,14 114,20 Z )~"
        R"~(M130,20 C135,13 143,5 150,0 L156,0 C149,6 141,14 136,20 Z M152,20 C157,13 165,5 172,0 L178,0 C171,6 163,14 158,20 Z )~"
        R"~(M174,20 C179,13 187,5 194,0 L200,0 C193,6 185,14 180,20 Z"/></svg>)~"},
    {"Dots", true, "EC5B1C5B-4C15-4A82-B600-6091A6C279F1", "258BE092-3EAB-4FAF-8795-B6FA09591AB1",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10"><circle cx="10" cy="5" r="4"/></svg>)~"},
    {"Chain", true, "239327F6-518B-4402-9998-0F089487538F", "8700EF61-5C15-485D-9A4D-F9326FE00E82",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 12">)~"
        R"~(<path style="fill-rule:evenodd;" d="M1,6 A10,5.5 0 1 0 21,6 A10,5.5 0 1 0 1,6 Z )~"
        R"~(M5,6 A6,2 0 1 0 17,6 A6,2 0 1 0 5,6 Z"/>)~"
        R"~(<rect x="19" y="5" width="15" height="2" rx="1"/></svg>)~"},
    {"Stitches", true, "D2AF5DB2-E975-475B-9740-69662928C8B6", "D08FED1A-FF1B-4BF2-BE59-8045FE22CF3A",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">)~"
        R"~(<polygon points="3,7 15,2 17,3.5 5,8.5"/></svg>)~"},
    {"Scallops", true, "A1491AD6-C626-407B-98B8-4F977FE4FB28", "0A6E52E6-DCE3-4D3A-835D-538B882A872A",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">)~"
        R"~(<path d="M0,10 C0,4.5 4.5,0 10,0 C15.5,0 20,4.5 20,10 L17,10 C17,6.1 13.9,3 10,3 )~"
        R"~(C6.1,3 3,6.1 3,10 Z"/></svg>)~"},
    {"Zigzag", true, "D158B5CC-CC2F-44C3-9000-259EE749CC54", "9700D32C-242C-481E-AB18-A77A82D0231F",
        R"~(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">)~"
        R"~(<polygon points="0,10 10,0 20,10 17,10 10,3 3,10"/></svg>)~"},
};

/// \a brush's definition, its settings as a document's brushes have them.
PaintBrushDefinition definitionOf(const OwnBrush& brush, int index)
{
    std::string json = "{}";
    const auto set = [&json](const char* key, const std::string& value) { json = withJsonMember(json, key, value); };
    set("FlashArtBrushMinorVersion", "0");
    set("FlashArtBrushVersion", "1");
    set("FlashPaintBrushType", brush.pattern ? "PatternBrush" : "ArtBrush");
    if (brush.pattern)
    {
        set("FlipAcross", "0");
        set("FlipAlong", "0");
        set("adjustment", "0");
        set("cornerValue", "2");
    }
    else
    {
        set("artDirection", "0");
        set("flipArt", "false");
        set("foldAdjust", "1");
    }
    set("guid", brush.guid);
    if (!brush.pattern)
        set("leftEdgePercentage", brush.leftGuide ? brush.leftGuide : "0.00");
    set("maximumTiltWidthPercentage", "100.00");
    set("maximumWidthPercentage", "100.00");
    set("minimumTiltWidthPercentage", "1.00");
    set("minimumWidthPercentage", "1.00");
    set("name", brush.name);
    if (brush.pattern)
    {
        set("spacing", "0");
    }
    else
    {
        set("rightEdgePercentage", brush.rightGuide ? brush.rightGuide : "100.00");
        set("scaleOptionm", brush.leftGuide ? "2" : "1");
    }
    set("svgData", brush.svg);
    set("svgID", brush.svgId);
    return paintBrushDefinition(index, json);
}

} // namespace

const std::vector<BrushLibraryEntry>& phoenixBrushes()
{
    static const std::vector<BrushLibraryEntry> brushes = []() {
        std::vector<BrushLibraryEntry> entries;
        int index = 0;
        for (const OwnBrush& brush : kOwnBrushes)
        {
            BrushLibraryEntry entry;
            entry.category = "Phoenix";
            entry.definition = definitionOf(brush, ++index);
            entry.builtIn = true;
            entries.push_back(std::move(entry));
        }
        return entries;
    }();
    return brushes;
}

} // namespace fla
