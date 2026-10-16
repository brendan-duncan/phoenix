#include "test_util.h"

#include "../src/edit/pdf_objects.h"
#include "../src/edit/postscript_import.h"
#include "../src/edit/vector_import.h"

#include "zlib.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> deflate(const std::string& text)
{
    uLongf size = compressBound(static_cast<uLong>(text.size()));
    std::vector<uint8_t> out(size);
    compress(out.data(), &size, reinterpret_cast<const Bytef*>(text.data()), static_cast<uLong>(text.size()));
    out.resize(size);
    return out;
}

/// A PDF whose objects are written as given (each "N 0 obj ... endobj"),
/// with a cross-reference table pointing at them and a trailer naming the
/// catalog, object 1.
std::vector<uint8_t> pdfOf(const std::vector<std::string>& objects)
{
    std::string file = "%PDF-1.5\r%\xE2\xE3\xCF\xD3\r\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        offsets.push_back(file.size());
        file += std::to_string(i + 1) + " 0 obj\r" + objects[i] + "\rendobj\r";
    }
    const size_t table = file.size();
    file += "xref\r\n0 " + std::to_string(objects.size() + 1) + "\r\n0000000000 65535 f\r\n";
    for (size_t offset : offsets)
    {
        char line[32];
        std::snprintf(line, sizeof(line), "%010zu 00000 n\r\n", offset);
        file += line;
    }
    file += "trailer\r<</Size " + std::to_string(objects.size() + 1) + "/Root 1 0 R>>\rstartxref\r" +
        std::to_string(table) + "\r%%EOF\r";
    return std::vector<uint8_t>(file.begin(), file.end());
}

std::string stream(const std::string& dictionaryExtra, const std::vector<uint8_t>& data)
{
    return "<</Length " + std::to_string(data.size()) + dictionaryExtra + ">>stream\r\n" +
        std::string(data.begin(), data.end()) + "\r\nendstream";
}

/// \a plain encrypted as Type 1 fonts are, with \a key (55665 for eexec,
/// 4330 for charstrings), behind four bytes of padding.
std::string type1Encrypt(const std::string& plain, uint32_t key)
{
    std::string out;
    uint32_t r = key;
    for (const char ch : std::string(4, '\x2A') + plain)
    {
        const uint8_t cipher = static_cast<uint8_t>(static_cast<uint8_t>(ch) ^ (r >> 8));
        out += static_cast<char>(cipher);
        r = ((cipher + r) * 52845u + 22719u) & 0xFFFFu;
    }
    return out;
}

std::string ascii85(const std::vector<uint8_t>& data)
{
    std::string out;
    for (size_t i = 0; i < data.size(); i += 4)
    {
        uint32_t group = 0;
        const size_t n = std::min<size_t>(4, data.size() - i);
        for (size_t k = 0; k < 4; ++k)
            group = (group << 8) | (k < n ? data[i + k] : 0);
        char chars[5];
        for (int k = 4; k >= 0; --k)
        {
            chars[k] = static_cast<char>('!' + group % 85);
            group /= 85;
        }
        out.append(chars, n + 1);
    }
    return out + "~>";
}

} // namespace

TEST(pdf_objects_read_compact_dictionaries_and_streams)
{
    // Illustrator writes dictionaries without spaces, a page dictionary
    // nesting others before its /Contents.
    // A spot color (a separation, its tint through a function to CMYK)
    // paints the second path.
    const std::string content = "1 0 0 rg 10 10 m 90 10 l 90 90 l h f /CS0 cs 1 scn 0 0 m 5 0 l 5 5 l h f";
    const std::vector<uint8_t> file = pdfOf({
        "<</Pages 2 0 R/Type/Catalog>>",
        "<</Count 1/Kids[3 0 R]/Type/Pages/MediaBox[0 0 100 100]>>",
        "<</Parent 2 0 R/Resources<</ExtGState<</GS0 5 0 R>>/ColorSpace<</CS0[/Separation/PANTONE#20213#20C/DeviceCMYK"
        "<</FunctionType 2/Domain[0 1]/C0[0 0 0 0]/C1[0 1 0 0]/N 1>>]>>>>/PieceInfo<</Illustrator 6 0 R>>"
        "/Contents 4 0 R/Type/Page>>",
        stream("/Filter/FlateDecode", deflate(content)),
        "<</Type/ExtGState/ca 1>>",
        "<</LastModified(D:2026)>>",
    });
    fla::pdf::File pdf;
    std::string error;
    CHECK(pdf.open(file, error));
    const auto pages = pdf.pages();
    CHECK(pages.size() == 1);
    if (pages.empty())
        return;
    // The page inherits its box from the tree.
    const fla::pdf::Object* box = pages[0].get("MediaBox");
    CHECK(box && box->items.size() == 4 && box->items[2].number == 100);
    const fla::pdf::Object* contents = pages[0].get("Contents");
    CHECK(contents && contents->type == fla::pdf::Object::Type::Reference && contents->reference == 4);
    const fla::pdf::Object* stream4 = pdf.object(4);
    CHECK(stream4 && stream4->type == fla::pdf::Object::Type::Stream);
    if (stream4)
    {
        const std::vector<uint8_t> data = pdf.decoded(*stream4);
        CHECK(std::string(data.begin(), data.end()) == content);
    }

    const fla::vectors::Artwork art = fla::vectors::readPdf(file);
    CHECK(art.paths.size() == 2);
    CHECK(art.width == 100 && art.height == 100);
    if (art.paths.size() == 2)
    {
        CHECK(art.paths[0].fill && (*art.paths[0].fill)[0] == 255 && (*art.paths[0].fill)[2] == 0);
        // Full tint: the alternate's magenta, not white.
        CHECK(art.paths[1].fill && (*art.paths[1].fill)[0] == 255 && (*art.paths[1].fill)[1] == 0 &&
              (*art.paths[1].fill)[2] == 255);
    }
}

TEST(pdf_objects_survive_damaged_tables_lengths_and_filters)
{
    const auto bytes = [](const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); };
    const std::string catalog = "%PDF-1.5\n1 0 obj <</Type/Catalog/Pages 2 0 R>> endobj\n"
                                "2 0 obj <</Type/Pages/Kids[3 0 R 3 0 R 3 0 R]/Count 1>> endobj\n"
                                "3 0 obj <</Type/Page/MediaBox[0 0 10 10]>> endobj\n";
    fla::pdf::File pdf;
    std::string error;
    // A table claiming two billion entries, cut off by the end of the file.
    std::string file = catalog;
    file += "xref\n0 2000000000\n";
    CHECK(pdf.open(bytes(file + "startxref\n" + std::to_string(catalog.size()) + "\n"), error));
    // A cross-reference stream whose rows are no bytes wide.
    file = catalog + "4 0 obj <</Type/XRef/W[0 0 0]/Size 2000000000/Root 1 0 R/Length 1>>stream\nx\nendstream endobj\n";
    CHECK(pdf.open(bytes(file + "startxref\n" + std::to_string(catalog.size()) + "\n"), error));
    // Two tables each naming the other as XRefStm and Prev.
    file = catalog;
    const size_t first = file.size();
    file += "xref\n0 1\n0000000000 65535 f \ntrailer <</Root 1 0 R/Prev SECOND____/XRefStm SECOND____>>\n";
    const size_t second = file.size();
    file += "xref\n0 1\n0000000000 65535 f \ntrailer <</Root 1 0 R/Prev FIRST_____/XRefStm FIRST_____>>\n";
    // Ten digits, so the offsets stay where they were.
    for (const auto& [mark, at] : {std::pair<std::string, size_t>{"SECOND____", second}, {"FIRST_____", first}})
        for (size_t p; (p = file.find(mark)) != std::string::npos;)
        {
            char digits[16];
            std::snprintf(digits, sizeof(digits), "%010zu", at);
            file.replace(p, mark.size(), digits);
        }
    CHECK(pdf.open(bytes(file + "startxref\n" + std::to_string(first) + "\n"), error));
    // The same page listed three times is one page.
    CHECK(pdf.open(bytes(catalog), error) && pdf.pages().size() == 1);

    // A length near 2^64, and predictor parameters past reason.
    const std::string data = "hello";
    file = catalog + "4 0 obj <</Length 18446744073709551615>>stream\n" + data + "\nendstream endobj\n" +
        "5 0 obj <</Filter/FlateDecode/DecodeParms<</Predictor 12/Colors 1000000000/Columns 1000000000"
        "/BitsPerComponent 1000>>/Length 5>>stream\nhello\nendstream endobj\n";
    CHECK(pdf.open(bytes(file), error));
    const fla::pdf::Object* stream = pdf.object(4);
    CHECK(stream && stream->type == fla::pdf::Object::Type::Stream);
    if (stream)
    {
        const std::vector<uint8_t> decoded = pdf.decoded(*stream);
        CHECK(std::string(decoded.begin(), decoded.end()) == data);
    }
    if (const fla::pdf::Object* predicted = pdf.object(5))
        CHECK(pdf.decoded(*predicted).empty());
}

TEST(illustrator_files_without_pdf_content_come_in_from_their_private_data)
{
    // The page draws only Illustrator's note; the art is in its private
    // data, Illustrator's own format, compressed.
    const std::string native = "%!PS-Adobe-3.0\r%%BoundingBox: 0 -100 100 0\r%AI5_FileFormat 9.0\r%%EndSetup\r"
                               "%AI5_BeginLayer\r1 1 1 1 0 0 0 0 79 128 255 0 50 0 Lb\r(Layer 1) Ln\r"
                               "0 0 1 0 0 0 1 Xa\r10 -10 m\r90 -10 L\r90 -90 L\r10 -90 L\rf\rLB\r%AI5_EndLayer--\r";
    std::string privateData = "%%BoundingBox: 0 -100 100 0\r%AI12_CompressedData";
    const std::vector<uint8_t> packed = deflate(native);
    privateData.append(packed.begin(), packed.end());
    const std::string note = "BT /F1 12 Tf (This is an Adobe Illustrator File that was saved without PDF Content.) Tj ET";
    const std::vector<uint8_t> file = pdfOf({
        "<</Pages 2 0 R/Type/Catalog>>",
        "<</Count 1/Kids[3 0 R]/Type/Pages>>",
        "<</Parent 2 0 R/MediaBox[0 0 100 100]/PieceInfo<</Illustrator 5 0 R>>/Contents 4 0 R/Type/Page>>",
        stream("", std::vector<uint8_t>(note.begin(), note.end())),
        "<</Private 6 0 R>>",
        "<</AIPrivateData1 7 0 R/NumBlock 1>>",
        stream("", std::vector<uint8_t>(privateData.begin(), privateData.end())),
    });
    const fla::vectors::Artwork art = fla::vectors::readPdf(file);
    CHECK(art.paths.size() == 1);
    if (!art.paths.empty())
    {
        // Xa's last three numbers are its RGB: blue.
        CHECK(art.paths[0].fill && (*art.paths[0].fill)[2] == 255 && (*art.paths[0].fill)[0] == 0);
        // The page's top left is the stage's origin.
        const fla::Point& first = art.paths[0].subpaths[0].segments[0].points[0];
        CHECK(first.x == 10 && first.y == 10);
    }
}

TEST(illustrator_files_without_pdf_content_read_zstandard_private_data)
{
    // Newer files compress their own data with Zstandard. This frame holds
    // ten small black squares (Huffman literals and repeated matches).
    const uint8_t frame[] = {
        0x28, 0xB5, 0x2F, 0xFD, 0x60, 0xEF, 0x00, 0x4D, 0x05, 0x00, 0xF2, 0x86, 0x18, 0x1C, 0x60, 0x6B,
        0xDB, 0x00, 0x2C, 0x10, 0x54, 0xC7, 0x5B, 0xB1, 0x83, 0x74, 0x8D, 0x7B, 0x51, 0x12, 0x24, 0xA8,
        0x89, 0x84, 0x5A, 0x4C, 0xC3, 0x56, 0x07, 0x8B, 0x52, 0x78, 0xAD, 0xB5, 0xC6, 0x18, 0x63, 0x6B,
        0xAD, 0xA5, 0x94, 0x52, 0xDD, 0xDD, 0x29, 0xA5, 0x84, 0x10, 0x42, 0xCC, 0xCC, 0xBB, 0xBB, 0x09,
        0xE5, 0x37, 0x16, 0xAE, 0xFB, 0xC6, 0xE6, 0xB8, 0xF8, 0x5E, 0xB1, 0xF1, 0x38, 0x8B, 0x41, 0x71,
        0x83, 0x4C, 0x54, 0x40, 0xDA, 0x3B, 0x88, 0xC6, 0xF1, 0x92, 0x85, 0xC1, 0x24, 0x87, 0x26, 0x08,
        0x12, 0x75, 0xAF, 0xD7, 0x07, 0x1E, 0x0F, 0x51, 0x38, 0x46, 0x71, 0xCF, 0x43, 0x32, 0x03, 0x2A,
        0x20, 0xA0, 0x83, 0x14, 0x0E, 0x45, 0xF2, 0xCD, 0x2C, 0x93, 0x74, 0x33, 0xCB, 0x92, 0xDD, 0xC4,
        0xB2, 0xE4, 0x9B, 0xB1, 0x24, 0xF9, 0x66, 0x96, 0x49, 0xBA, 0x99, 0x65, 0xC9, 0x6E, 0x62, 0x59,
        0xF2, 0xCD, 0x58, 0x92, 0x7C, 0x33, 0xCB, 0x24, 0xDD, 0xCC, 0xB2, 0x20, 0xBB, 0x47, 0x62, 0x6E,
        0x59, 0x5A, 0xF3, 0x31, 0x32, 0x5B, 0x8D, 0x94, 0xB9, 0xE4, 0xF8, 0x33, 0x51, 0xF2, 0x48, 0xBC,
        0x5F, 0x9F, 0x09,
    };
    std::string privateData = "%%BoundingBox: 0 0 100 100\r%AI24_ZStandard_Data";
    privateData.append(reinterpret_cast<const char*>(frame), sizeof(frame));
    const fla::vectors::Artwork art =
        fla::vectors::readIllustratorPrivateData(std::vector<uint8_t>(privateData.begin(), privateData.end()));
    CHECK(art.paths.size() == 10);
    if (art.paths.size() == 10)
    {
        const fla::Point& p = art.paths[9].subpaths[0].segments[0].points[0];
        CHECK(p.x == 90 && p.y == 90);
    }
    // Damaged data: a note, nothing read, no crash.
    std::string damaged = "%AI24_ZStandard_Data";
    damaged.append(reinterpret_cast<const char*>(frame), 40);
    damaged.append(100, '\xFF');
    const fla::vectors::Artwork none =
        fla::vectors::readIllustratorPrivateData(std::vector<uint8_t>(damaged.begin(), damaged.end()));
    CHECK(none.paths.empty());
}

TEST(illustrator_format_reads_layers_compounds_gradients_clips_and_text)
{
    const std::string text =
        "%!PS-Adobe-3.0\r%%Creator: Adobe Illustrator(R) 8.0\r%%BoundingBox: 0 0 200 200\r%AI5_FileFormat 4.0\r"
        "%AI5_BeginGradient: (Ramp\\012 1)\r(Ramp\\012 1) 0 2 Bd\r[\r<\rFFEE\r>\r"
        "0 0 0 0 1 50 100 %_BS\r%_0 0 0 0 1 50 100 Bs\r0 0 0 1 1 50 0 %_BS\r%_0 0 0 1 1 50 0 Bs\rBD\r%AI5_EndGradient\r"
        "%%EndSetup\r"
        // A hidden layer: left out.
        "%AI5_BeginLayer\r0 1 1 1 0 0 0 0 79 128 255 0 50 0 Lb\r(Hidden) Ln\r0 g\r0 0 m\r10 0 L\r10 10 L\rf\rLB\r"
        "%AI5_BeginLayer\r1 1 1 1 0 0 0 0 79 128 255 0 50 0 Lb\r(Art) Ln\r"
        // A compound path: one shape, its hole.
        "*u\r0 0 0 1 k\r0 0 m\r100 0 L\r100 100 L\r0 100 L\rf\r25 25 m\r75 25 L\r75 75 L\r25 75 L\rf\r*U\r"
        // A gradient fill, placed by Xm.
        "Bb\r2 (Ramp\\012 1) 0 0 0 1 1 0 0 1 0 0 Bg\r100 0 0 100 100 100 Xm\r100 100 m\r200 100 L\r200 200 L\rf\r0 BB\r"
        // A clip group: the square cut to a smaller one.
        "q\r120 0 m\r150 0 L\r150 30 L\r120 30 L\rh\rW\rn\r0 1 0 0 k\r100 0 m\r200 0 L\r200 50 L\r100 50 L\rf\rQ\r"
        // Point text.
        "0 To\r1 0 0 1 10 180 0 Tp\rTP\r0 g\r/_Helvetica-Bold 12 Tf\r(Hello) Tx\r(\\r) TX\rTO\r"
        "LB\r%AI5_EndLayer--\r%%Trailer\r";
    const fla::vectors::Artwork art = fla::vectors::readIllustrator(text);
    CHECK(art.paths.size() == 3);
    if (art.paths.size() != 3)
        return;
    CHECK(art.paths[0].subpaths.size() == 2);
    CHECK(art.paths[0].fill && (*art.paths[0].fill)[0] == 0);
    CHECK(art.paths[1].gradient && art.paths[1].gradient->stops.size() == 2);
    if (art.paths[1].gradient)
    {
        // Ramp points 0 and 100, sorted: black at 0, white at 1.
        CHECK(art.paths[1].gradient->stops[0].offset == 0.0 && art.paths[1].gradient->stops[0].color[0] == 0);
        CHECK(art.paths[1].gradient->stops[1].color[0] == 255);
    }
    CHECK(art.paths[2].clips.size() == 1 && art.paths[2].clips[0].size() == 1);
    CHECK(art.texts.size() == 1);
    if (!art.texts.empty())
    {
        CHECK(art.texts[0].text == "Hello" && art.texts[0].family == "Helvetica" && art.texts[0].bold);
        CHECK(art.texts[0].size == 12 && art.texts[0].placement.tx == 10 && art.texts[0].placement.ty == 20);
    }
}

TEST(postscript_programs_run_and_paint)
{
    // Procedures, loops, colors, arcs, a clip, text and an image.
    const std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 200 100\n"
        "/box { /h exch def /w exch def /y exch def /x exch def x y moveto w 0 rlineto 0 h rlineto w neg 0 rlineto "
        "closepath } bind def\n"
        "0 1 2 { /i exch def gsave i 60 mul 10 translate 1 i 0.4 mul sub 0 0 setrgbcolor 0 0 40 40 box fill grestore } for\n"
        "/DeviceCMYK setcolorspace 0 0 0 1 setcolor 3 setlinewidth newpath 150 50 20 0 360 arc stroke\n"
        "[/Separation (Spot) /DeviceRGB {0 exch 0}] setcolorspace 1 setcolor 0 0 10 10 rectfill\n"
        "gsave 0 0 50 50 rectclip 0 setgray 0 0 100 100 rectfill grestore\n"
        "/Helvetica findfont 20 scalefont setfont 10 80 moveto (Hi) show\n"
        "gsave 100 0 translate 20 20 scale 2 2 8 [2 0 0 -2 0 2] {<00FF80FF>} image grestore\n"
        "showpage\n";
    const double box[4] = {0, 0, 200, 100};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
    CHECK(art.notes.empty());
    for (const std::string& n : art.notes)
        std::printf("    note: %s\n", n.c_str());
    // Three boxes, the circle, the spot square and the clipped square.
    CHECK(art.paths.size() == 6);
    if (art.paths.size() == 6)
    {
        CHECK((*art.paths[0].fill)[0] == 255 && (*art.paths[2].fill)[0] < 60);
        CHECK(art.paths[3].stroke && !art.paths[3].fill && art.paths[3].strokeWidth == 3);
        // The tint transform makes the spot's tint its RGB's green.
        CHECK(art.paths[4].fill && (*art.paths[4].fill)[1] == 255 && (*art.paths[4].fill)[0] == 0);
        CHECK(art.paths[5].clips.size() == 1 && art.paths[0].clips.empty());
        // y goes down from the page's top: the first box's top at 100-50.
        const fla::Point& p = art.paths[0].subpaths[0].segments[0].points[0];
        CHECK(p.x == 0 && p.y == 90);
    }
    CHECK(art.texts.size() == 1 && art.texts[0].text == "Hi" && art.texts[0].size == 20);
    CHECK(art.images.size() == 1);
    if (!art.images.empty())
    {
        CHECK(art.images[0].width == 2 && art.images[0].rgba[0] == 0 && art.images[0].rgba[4] == 255);
        // The unit square onto the 20-point square at (100, 0).
        CHECK(art.images[0].placement.a == 20 && art.images[0].placement.d == 20);
        CHECK(art.images[0].placement.tx == 100 && art.images[0].placement.ty == 80);
    }
}

TEST(postscript_hostile_programs_stop_cleanly)
{
    // Each stops with an error (or runs to its end) rather than hanging,
    // overflowing the stack or reading out of bounds.
    const std::string deepBraces = std::string(100000, '{');
    const char* programs[] = {
        "0 1e-300 1 {} for",
        "/x /x cvx def x",
        "(x) cvx /x exch def x",
        deepBraces.c_str(),
        "0 0 1 5000 { pop [ exch ] } for",
        "-2147483648 -1 idiv -2147483648 -1 mod",
        "(abc) 5 search",
        "5 (abc) anchorsearch",
        "{} 5 kshow",
        "5 (xx) readstring",
        "5 (xx) readhexstring",
        "5 (xx) readline",
        "5 read",
        "true 5 if",
        "{gsave} loop",
        "{1} loop",
        "0 0 moveto 0 0 10 -1e20 0 arc 0 0 10 0 1e20 arcn",
        "1 1 8 [1 0 0 1 0 0] (x) false 7 colorimage",
        "/DeviceRGB setcolorspace << /ImageType 1 /Width 1 /Height 1 /BitsPerComponent 8 /ImageMatrix [1 0 0 1 0 0] "
        "/DataSource [(a)] /MultipleDataSources true >> image",
        "1 1 3 [1 0 0 1 0 0] (x) image",
        "20000 20000 8 [1 0 0 1 0 0] (x) image",
        "<< /Width 1e300 /Height 1e300 /DataSource (x) >> imagemask",
        "/P << /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1 1] /XStep 1 /YStep 1 "
        "/PaintProc { pop P setpattern 0 0 1 1 rectfill } >> matrix makepattern def P setpattern 0 0 40 40 rectfill",
        "<< /PatternType 1 /BBox [0 0 1 1] /XStep 1e-300 /YStep 1e300 /PaintProc {} >> matrix makepattern setpattern "
        "0 0 100 100 rectfill",
        "<< /ShadingType 2 /Coords [0 0 1 0] /Function << /FunctionType 0 /Domain [0 1] /Range [0 1] "
        "/Size [100000000] /BitsPerSample 32 /DataSource (x) >> >> shfill",
        "<< /ShadingType 3 /Coords [0 0 0 0 0 -5] /Function << /FunctionType 3 /Functions [] >> >> shfill",
        "currentfile 100 { /ASCIIHexDecode filter } repeat",
        "currentfile eexec 0123456789abcdef",
        "(abc) eexec (abc) eexec",
        "[/Indexed /DeviceRGB 1e9 {}] setcolorspace 5 setcolor 0 0 1 1 rectfill",
    };
    const double box[4] = {0, 0, 100, 100};
    for (const char* program : programs)
    {
        const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
        CHECK(art.images.empty());
    }
    // An empty procedure looped still ends at the step limit.
    const fla::vectors::Artwork art = fla::vectors::readPostScript("{} loop", box);
    CHECK(art.notes.size() == 1 && art.notes[0].find("limitcheck") != std::string::npos);
}

TEST(postscript_text_moves_on_by_the_fonts_widths)
{
    // Helvetica's A is 667 thousandths of the size, Times-Roman's 722,
    // Courier's every glyph 600; ashow adds its spacing after each glyph.
    const std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 400 100\n"
        "/Helvetica findfont 10 scalefont setfont 10 50 moveto (AA) show (B) show\n"
        "/Times-Roman findfont 10 scalefont setfont 10 30 moveto (A) show (B) show\n"
        "/Courier findfont 10 scalefont setfont 10 10 moveto 2 0 (AA) ashow (B) show\n"
        "/Helvetica findfont 10 scalefont setfont (AA) stringwidth pop 100 mul round 10 10 moveto 0 0 rlineto "
        "/w exch def w 1334 eq { 0 0 10 10 rectfill } if\n";
    const double box[4] = {0, 0, 400, 100};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
    CHECK(art.texts.size() == 6);
    if (art.texts.size() == 6)
    {
        CHECK(std::fabs(art.texts[1].placement.tx - (10 + 13.34)) < 1e-9);
        CHECK(std::fabs(art.texts[3].placement.tx - (10 + 7.22)) < 1e-9);
        CHECK(std::fabs(art.texts[5].placement.tx - (10 + 12 + 4)) < 1e-9);
    }
    // stringwidth gives the same width.
    CHECK(art.paths.size() == 1);
}

TEST(illustrator_patterns_fill_with_their_tiles)
{
    // A 10-point tile with a magenta square in its corner, filling a 30 by
    // 20 rectangle; a plain fill after it is plain again.
    const std::string text = "%!PS-Adobe-3.0\r%%BoundingBox: 0 0 100 100\r%AI5_FileFormat 3\r"
                             "%AI3_BeginPattern: (Dots)\r(Dots) 0 0 10 10 [\r%AI3_Tile\r(0 O 0 R 0 1 0 0 k\r) @\r"
                             "(\r0 0 m\r5 0 L\r5 5 L\r0 5 L\rf\r) &\r] E\r%AI3_EndPattern\r%%EndSetup\r"
                             "(Dots) 0 0 1 1 0 0 0 0 0 [1 0 0 1 0 0] p\r0 0 m\r30 0 L\r30 20 L\r0 20 L\rf\r"
                             "0 g\r50 50 m\r60 50 L\r60 60 L\rf\r";
    const fla::vectors::Artwork art = fla::vectors::readIllustrator(text);
    CHECK(art.notes.empty());
    size_t magenta = 0;
    for (const fla::vectors::Painted& p : art.paths)
        if (p.fill && (*p.fill)[0] == 255 && (*p.fill)[1] == 0 && (*p.fill)[2] == 255 && p.clips.size() == 1)
            ++magenta;
    // Cells from one before the area to one past it, each cut to it.
    CHECK(magenta == 20);
    CHECK(art.paths.size() == 21 && art.paths.back().fill && (*art.paths.back().fill)[0] == 0);
    if (!art.paths.empty())
    {
        // The first tile's square: (-10, -10) on the page, to the stage.
        const fla::Point& p = art.paths[0].subpaths[0].segments[0].points[0];
        CHECK(p.x == -10 && p.y == 110);
    }
}

TEST(illustrator_images_come_in_from_their_samples)
{
    // Illustrator 9's: RGB samples, then an alpha plane; Illustrator 8's:
    // a JPEG in base-85 lines behind "%".
    std::string text = "%!PS-Adobe-3.0\r%%BoundingBox: 0 0 100 100\r%AI5_FileFormat 9.0\r%%EndSetup\r"
                       "%AI5_BeginRaster\r() 1 XG\r[ 1 0 0 1 10 90 ] 2 1 0 Xh\r"
                       "[ 1 0 0 1 10 90 ] 0 0 2 1 2 1 8 3 1 0 1 0 4 4 0 0\r%%BeginData: 9\rXI\n";
    text += std::string("\xFF\x00\x00\x00\xFF\x00\xFF\x00", 8);
    text += "%%EndData\rXH\r%AI5_EndRaster\r";
    const std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xD9};
    text += "[0.5 0 0 0.5 20 60] 0 0 1 1 1 1 8 4 0 0 514 0 XI\r%" + ascii85(jpeg) + "\rXH\r";
    text += "0 0 m\r1 0 L\r1 1 L\rf\r";
    const fla::vectors::Artwork art = fla::vectors::readIllustrator(text);
    CHECK(art.notes.empty());
    CHECK(art.images.size() == 2 && art.paths.size() == 1);
    if (art.images.size() == 2)
    {
        const fla::vectors::Image& rgb = art.images[0];
        CHECK(rgb.width == 2 && rgb.height == 1 && rgb.rgba.size() == 8);
        if (rgb.rgba.size() == 8)
            CHECK(rgb.rgba[0] == 255 && rgb.rgba[3] == 255 && rgb.rgba[5] == 255 && rgb.rgba[7] == 0);
        // Its top left at (10, 90) on the page, a point a pixel.
        CHECK(rgb.placement.a == 2 && rgb.placement.d == 1 && rgb.placement.tx == 10 && rgb.placement.ty == 10);
        CHECK(art.images[1].encoded == jpeg && art.images[1].inkCmyk);
        CHECK(art.images[1].placement.a == 0.5 && art.images[1].placement.ty == 40);
    }
}

TEST(illustrator_area_and_path_text_come_in)
{
    // Area text: its lines where Td places them, the frame not drawn.
    // Path text: a glyph at a time along its path.
    const std::string text = "%!PS-Adobe-3.0\r%%BoundingBox: 0 0 100 100\r%AI5_FileFormat 3\r%%EndSetup\r"
                             "1 To\r1 0 0 1 10 80 0 Tp\r10 80 m\r90 80 L\r90 40 L\r10 40 L\rn\rTP\r0 -10 Td\r"
                             "/_Helvetica 10 Tf\r0 g\r(One) Tx\r( \\r) Tx\r0 -12 Td\r(Two) Tx\rTO\r"
                             "2 To\r1 0 0 1 10 50 0 Tp\r10 50 m\r90 50 L\rN\rTP\r/_Helvetica 10 Tf\r(AB) Tx\rTO\r";
    const fla::vectors::Artwork art = fla::vectors::readIllustrator(text);
    CHECK(art.paths.empty());
    CHECK(art.texts.size() == 4);
    if (art.texts.size() == 4)
    {
        CHECK(art.texts[0].text == "One" && art.texts[0].placement.tx == 10 && art.texts[0].placement.ty == 30);
        CHECK(art.texts[1].text == "Two" && art.texts[1].placement.ty == 42);
        CHECK(art.texts[2].text == "A" && art.texts[2].placement.tx == 10 && art.texts[2].placement.ty == 50);
        // Helvetica's A is 667 thousandths.
        CHECK(art.texts[3].text == "B");
        CHECK_NEAR(art.texts[3].placement.tx, 16.67);
    }
}

TEST(illustrator_11_text_comes_in_where_the_text_engine_laid_it)
{
    // The text engine's document: a font, a point text frame at (1010, 1020)
    // from a ruler origin of (1000, 1100), and a story of two lines, a tab
    // in the first, laid out by its strikes.
    const auto utf16 = [](const std::string& ascii) {
        std::string out = "\xFE\xFF";
        for (const char c : ascii)
        {
            out += '\0';
            out += c;
        }
        return out;
    };
    const std::string doc =
        "/DocumentResources << /FontSet << /Resources [ << /Resource << /Identifier << /Name (" + utf16("Times-Roman") +
        ") /Type 0 >> >> >> ] >> /StyleSheetSet << /Resources [ << /Resource << /Features << /Font 0 /FontSize 12.0 "
        ">> >> >> ] >> /TextFrameSet << /Resources [ << /Resource << /Bezier << /Points [ 0.0 0.0 ] >> /Data << /Type 0 "
        "/FrameMatrix [ 1.0 0.0 0.0 1.0 1010.0 1020.0 ] >> >> >> ] >> >> "
        "/DocumentObjects << /TextObjects [ << /Model << /Text (" + utf16("Hi\tX\rYo\r") + ") "
        "/StyleRun << /RunArray [ << /RunData << /StyleSheet << /Parent 0 /Features << /FontSize 10.0 /FillColor << "
        "/StreamTag /SimplePaint /Color << /Type 1 /Values [ 1.0 1.0 0.0 0.0 ] >> >> >> >> >> /Length 8 >> ] >> "
        "/ParagraphRun << /RunArray [ << /RunData << /ParagraphSheet << /Features << /TabStops << /TabStops [ << "
        "/TabAdvance 50.0 /TabType 0 >> ] >> >> >> >> /Length 8 >> ] >> >> "
        "/View << /Frames [ << /Resource 0 >> ] /Strikes [ << /StreamTag /PathSelectGroupCharacter /Children [ << "
        "/StreamTag /FrameStrike /Frame 0 /Transform << /Origin [ 0.0 0.0 ] >> /Children [ "
        "<< /StreamTag /LineStrike /Transform << /Origin [ 0.0 0.0 ] >> /Children [ << /StreamTag /Segment "
        "/Mapping << /CharacterCount 5 >> >> ] >> "
        "<< /StreamTag /LineStrike /Transform << /Origin [ 0.0 12.0 ] >> /Children [ << /StreamTag /Segment "
        "/Mapping << /CharacterCount 3 >> >> ] >> ] >> ] >> ] >> >> ] >>";
    const std::string encoded = ascii85(std::vector<uint8_t>(doc.begin(), doc.end()));
    std::string text = "%!PS-Adobe-3.0\r%%BoundingBox: 0 0 100 100\r%AI5_FileFormat 14.0\r%AI11_BeginTextDocument\r"
                       "/AI11TextDocument : /ASCII85Decode ,\r";
    for (size_t i = 0; i < encoded.size(); i += 80)
        text += "%" + encoded.substr(i, 80) + "\r";
    text += "1000 1100 /RulerOrigin ,\r;\r%AI11_EndTextDocument\r%%EndSetup\r"
            "/AI11Text :\r0 /FreeUndo ,\r0 /FrameIndex ,\r0 /StoryIndex ,\r;\r";
    const fla::vectors::Artwork art = fla::vectors::readIllustrator(text);
    CHECK(art.notes.empty());
    CHECK(art.texts.size() == 3);
    if (art.texts.size() == 3)
    {
        CHECK(art.texts[0].text == "Hi" && art.texts[0].family == "Times" && art.texts[0].size == 10);
        CHECK(art.texts[0].color[0] == 255 && art.texts[0].color[1] == 0);
        CHECK(art.texts[0].placement.tx == 10 && art.texts[0].placement.ty == 20 && art.texts[0].placement.d == 1);
        // The tab stop 50 points along the line.
        CHECK(art.texts[1].text == "X" && art.texts[1].placement.tx == 60);
        CHECK(art.texts[2].text == "Yo" && art.texts[2].placement.ty == 32);
    }
}

TEST(illustrator_hostile_native_data_stops_cleanly)
{
    // A pattern that fills its own tile with itself, images claiming more
    // than they hold or past any size, a text document nested past reason
    // and one that is not one: each stops, nothing read out of bounds.
    const std::string deep = ascii85(std::vector<uint8_t>(200000, '['));
    const std::string texts[] = {
        "%%BoundingBox: 0 0 100 100\r%AI3_BeginPattern: (P)\r(P) 0 0 10 10 [\r(\r(P) 0 0 1 1 0 0 0 0 0 [1 0 0 1 0 0] p\r"
        "0 0 m\r10 0 L\r10 10 L\rf\r) &\r] E\r%AI3_EndPattern\r%%EndSetup\r"
        "(P) 0 0 1 1 0 0 0 0 0 [1 0 0 1 0 0] p\r0 0 m\r100 0 L\r100 100 L\rf\r",
        "%%EndSetup\r[ 1 0 0 1 0 0 ] 0 0 9 9 4000 4000 8 4 1 0 1 0 XI\n\x01\x02",
        "%%EndSetup\r[ 1 0 0 1 0 0 ] 0 0 9 9 1e300 -5 8 4 1 0 1 0 XI\n",
        "%%EndSetup\r[ 1 0 0 1 0 0 ] 0 0 1 1 1 1 8 4 0 0 514 0 XI\r%zz~>\r",
        "/AI11TextDocument : /ASCII85Decode ,\r%" + deep + "\r%%EndSetup\r/AI11Text :\r0 /StoryIndex ,\r;\r",
        "/AI11TextDocument : /ASCII85Decode ,\r%!!!!!~>\r%%EndSetup\r/AI11Text :\r7 /StoryIndex ,\r-3 /FrameIndex ,\r;\r",
    };
    for (const std::string& text : texts)
    {
        const fla::vectors::Artwork art = fla::vectors::readIllustrator(text);
        CHECK(art.paths.size() <= 25000);
    }
}

TEST(illustrator_eps_reads_its_private_data_behind_a_binary_header)
{
    // An Illustrator 9 EPS prints through Adobe's procedures; its own art
    // is base-85 zlib data after %AI9_PrivateDataBegin, each line behind %.
    const std::string native = "%%BoundingBox: 0 0 50 50\r%AI5_FileFormat 10.0\r%%EndSetup\r"
                               "%AI5_BeginLayer\r1 1 1 1 0 0 0 0 79 128 255 0 50 0 Lb\r"
                               "0 1 0 0 k\r0 0 m\r50 0 L\r50 50 L\rf\rLB\r";
    const std::string encoded = ascii85(deflate(native));
    std::string body = "%!PS-Adobe-3.0 EPSF-3.0\r%%Creator: Adobe Illustrator(R) 15.1\r%%BoundingBox: 0 0 50 50\r"
                       "Adobe_AGM_Core/ps gx\r%%EOF\r%AI9_PrivateDataBegin\r%!PS-Adobe-3.0 EPSF-3.0\r%AI9_DataStream\r";
    for (size_t i = 0; i < encoded.size(); i += 60)
        body += "%" + encoded.substr(i, 60) + "\r";
    body += "%AI9_PrivateDataEnd\r";
    std::vector<uint8_t> file(30, 0);
    const uint8_t magic[4] = {0xC5, 0xD0, 0xD3, 0xC6};
    std::copy(magic, magic + 4, file.begin());
    const auto put = [&file](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            file[at + static_cast<size_t>(i)] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    };
    put(4, 30);
    put(8, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    CHECK(fla::vectors::looksLikePostScript(file));
    const fla::vectors::Artwork art = fla::vectors::readPdf(file);
    CHECK(art.paths.size() == 1);
    if (!art.paths.empty())
        CHECK(art.paths[0].fill && (*art.paths[0].fill)[1] == 0 && (*art.paths[0].fill)[0] == 255);
}

TEST(postscript_shadings_come_in_as_gradients)
{
    // An axial shading filling a clip, and a radial one through a shading
    // pattern whose function is stitched from two.
    const std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 200 100\n"
        "gsave 0 0 100 100 rectclip\n"
        "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 100 0] /Extend [true true]\n"
        "   /Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >> shfill grestore\n"
        "<< /PatternType 2 /Shading << /ShadingType 3 /ColorSpace /DeviceGray /Coords [150 50 0 150 50 40]\n"
        "   /Function << /FunctionType 3 /Domain [0 1] /Bounds [0.5] /Encode [0 1 0 1] /Functions [\n"
        "     << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>\n"
        "     << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >> ] >> >> >>\n"
        "matrix makepattern setpattern 110 10 80 80 rectfill\n";
    const double box[4] = {0, 0, 200, 100};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
    CHECK(art.notes.empty());
    CHECK(art.paths.size() == 2);
    if (art.paths.size() != 2)
        return;
    // The clip's square, red to blue along x.
    const fla::vectors::Painted& axial = art.paths[0];
    CHECK(axial.gradient && !axial.gradient->radial && axial.clips.empty());
    if (axial.gradient)
    {
        CHECK(axial.gradient->stops.size() == 2);
        CHECK(axial.gradient->stops.front().color[0] == 255 && axial.gradient->stops.back().color[2] == 255);
        CHECK_NEAR(axial.gradient->placement.a, 100 / 1638.4);
        CHECK_NEAR(axial.gradient->placement.tx, 50);
    }
    // Black, white at the middle, black: on a circle of 40 at (150, 50).
    const fla::vectors::Painted& radial = art.paths[1];
    CHECK(radial.gradient && radial.gradient->radial);
    if (radial.gradient)
    {
        CHECK(radial.gradient->stops.size() >= 3 && radial.gradient->stops.front().color[0] == 0);
        bool white = false;
        for (const auto& stop : radial.gradient->stops)
            white = white || (std::fabs(stop.offset - 0.5) < 0.01 && stop.color[0] > 250);
        CHECK(white);
        CHECK_NEAR(radial.gradient->placement.a, 40 / 819.2);
        CHECK_NEAR(radial.gradient->placement.tx, 150);
        CHECK_NEAR(radial.gradient->placement.ty, 50);
    }
}

TEST(postscript_tiling_patterns_paint_their_tiles)
{
    // A colored tile (a red square in each 10-point cell), an uncolored one
    // painted green whatever its procedure sets, and one too fine to tile.
    const std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 200 100\n"
        "/cell { << /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10] /XStep 10 /YStep 10\n"
        "   /PaintProc { pop 1 0 0 setrgbcolor 0 0 5 5 rectfill } >> } def\n"
        "cell matrix makepattern setpattern 0 0 30 20 rectfill\n"
        "[/Pattern /DeviceRGB] setcolorspace\n"
        "<< /PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 10 10] /XStep 10 /YStep 10\n"
        "   /PaintProc { pop 1 0 0 setrgbcolor 0 0 5 5 rectfill } >> matrix makepattern\n"
        "0 1 0 4 -1 roll setcolor 100 0 20 20 rectfill\n"
        "<< /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 0.1 0.1] /XStep 0.1 /YStep 0.1\n"
        "   /PaintProc { pop 0 0 1 setrgbcolor 0 0 0.1 0.1 rectfill } >> matrix makepattern setpattern\n"
        "150 0 50 50 rectfill\n";
    const double box[4] = {0, 0, 200, 100};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
    size_t red = 0, green = 0, blue = 0, clipped = 0;
    for (const fla::vectors::Painted& p : art.paths)
    {
        if (!p.fill)
            continue;
        const auto& c = *p.fill;
        red += c[0] == 255 && c[1] == 0 && c[2] == 0;
        green += c[0] == 0 && c[1] == 255 && c[2] == 0;
        blue += c[0] == 0 && c[1] == 0 && c[2] == 255;
        clipped += p.clips.size() == 1;
    }
    // Cells from one before the area to one past it, each cut to it.
    CHECK(red == 20);
    CHECK(green == 16);
    CHECK(blue == 1);
    CHECK(clipped == 36);
    CHECK(art.notes.size() == 1 && art.notes[0].find("too fine") != std::string::npos);
}

TEST(postscript_image_masks_and_masked_images_come_in)
{
    const std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n"
        // A stencil in red: with polarity true its 1 bits paint.
        "1 0 0 setrgbcolor 8 2 true [8 0 0 -2 0 2] <F00F> imagemask\n"
        // A dictionary's, Decode [1 0]: the 1 bits paint.
        "<< /ImageType 1 /Width 8 /Height 1 /ImageMatrix [8 0 0 -1 0 1] /DataSource <0F> /Decode [1 0] >> imagemask\n"
        // An RGB image with a mask of its own: the second pixel masked out.
        "/DeviceRGB setcolorspace << /ImageType 3 /InterleaveType 3\n"
        "  /DataDict << /ImageType 1 /Width 2 /Height 1 /BitsPerComponent 8 /ImageMatrix [2 0 0 -1 0 1]\n"
        "     /DataSource <FF000000FF00> /Decode [0 1 0 1 0 1] >>\n"
        "  /MaskDict << /ImageType 1 /Width 2 /Height 1 /BitsPerComponent 1 /ImageMatrix [2 0 0 -1 0 1]\n"
        "     /DataSource <40> >> >> image\n"
        // One bit gray, and an indexed image.
        "2 1 1 [2 0 0 -1 0 1] <80> image\n"
        "[/Indexed /DeviceRGB 1 <FF000000FF00>] setcolorspace\n"
        "<< /ImageType 1 /Width 2 /Height 1 /BitsPerComponent 8 /ImageMatrix [2 0 0 -1 0 1] /DataSource <0001> >> image\n"
        // A JPEG behind its filter, the program reading on past it.
        "/DeviceRGB setcolorspace\n"
        "<< /ImageType 1 /Width 1 /Height 1 /BitsPerComponent 8 /ImageMatrix [1 0 0 -1 0 1] /Decode [0 1 0 1 0 1]\n"
        "   /DataSource currentfile /DCTDecode filter >> image\n";
    std::string jpeg = "\xFF\xD8\xFF\xDB";
    jpeg += std::string("\x00\x04\x00\x00", 4);
    jpeg += "\xFF\xDA";
    jpeg += std::string("\x00\x02", 2);
    jpeg += "\x12\x34\xFF\xD9";
    const std::string all = program + jpeg + "\n0 0 10 10 rectfill\n";
    const double box[4] = {0, 0, 100, 100};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(all, box);
    CHECK(art.notes.empty());
    CHECK(art.images.size() == 6);
    if (art.images.size() == 6)
    {
        const auto& stencil = art.images[0];
        CHECK(stencil.width == 8 && stencil.height == 2 && stencil.rgba[0] == 255 && stencil.rgba[1] == 0);
        CHECK(stencil.rgba[3] == 255 && stencil.rgba[4 * 4 + 3] == 0 && stencil.rgba[(8 + 4) * 4 + 3] == 255);
        const auto& decoded = art.images[1];
        CHECK(decoded.rgba[3] == 0 && decoded.rgba[4 * 4 + 3] == 255);
        const auto& masked = art.images[2];
        CHECK(masked.rgba[0] == 255 && masked.rgba[3] == 255 && masked.rgba[4 + 1] == 255 && masked.rgba[4 + 3] == 0);
        const auto& bit = art.images[3];
        CHECK(bit.rgba[0] == 255 && bit.rgba[4] == 0);
        const auto& indexed = art.images[4];
        CHECK(indexed.rgba[0] == 255 && indexed.rgba[1] == 0 && indexed.rgba[4] == 0 && indexed.rgba[5] == 255);
        CHECK(art.images[5].encoded.size() == jpeg.size() && art.images[5].encoded[0] == 0xFF);
    }
    // The square after the JPEG's data.
    CHECK(art.paths.size() == 1);
}

TEST(postscript_binary_planes_read_in_turn_after_their_operator)
{
    // Photoshop's way: a procedure a plane, each reading a row from the
    // program in turn, the data starting right after "image " (a zero byte
    // first, which is data, not white space).
    std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n/DeviceRGB setcolorspace\n"
        "/p1 1 string def /p2 1 string def /p3 1 string def\n"
        "<< /ImageType 1 /Width 1 /Height 2 /BitsPerComponent 8 /ImageMatrix [1 0 0 -2 0 2] /Decode [0 1 0 1 0 1]\n"
        "   /MultipleDataSources true\n"
        "   /DataSource [{currentfile p1 readstring pop} {currentfile p2 readstring pop} {currentfile p3 readstring pop}]\n"
        ">> image ";
    program += std::string("\x00\xFF\x00\xFF\x00\x00", 6);
    program += "\n0 0 1 1 rectfill\n";
    const double box[4] = {0, 0, 10, 10};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
    CHECK(art.images.size() == 1 && art.paths.size() == 1);
    if (!art.images.empty())
    {
        // Row one green, row two red.
        const auto& rgba = art.images[0].rgba;
        CHECK(rgba[0] == 0 && rgba[1] == 255 && rgba[2] == 0);
        CHECK(rgba[4] == 255 && rgba[5] == 0 && rgba[6] == 0);
    }
}

TEST(postscript_fonts_the_file_carries_give_their_widths)
{
    // A Type 1 font behind eexec: A 400 units wide, B 700, as their
    // charstrings' hsbw say (0 400 hsbw endchar).
    const std::string a = type1Encrypt(std::string("\x8B\xF8\x24\x0D\x0E", 5), 4330);
    const std::string b = type1Encrypt(std::string("\x8B\xF9\x50\x0D\x0E", 5), 4330);
    const std::string plain =
        "dup /Private 8 dict dup begin /RD{string currentfile exch readstring pop}executeonly def\n"
        "/ND{noaccess def}executeonly def /NP{noaccess put}executeonly def /lenIV 4 def\n"
        "2 index /CharStrings 3 dict dup begin\n/A " + std::to_string(a.size()) + " RD " + a + " ND\n/B " +
        std::to_string(b.size()) + " RD " + b + " ND\nend\nend\nreadonly put\nnoaccess put\n"
        "dup /FontName get exch definefont pop\nmark currentfile closefile\n";
    const std::string encrypted = type1Encrypt(plain, 55665);
    std::string hex;
    for (size_t i = 0; i < encrypted.size(); ++i)
    {
        char digits[3];
        std::snprintf(digits, sizeof(digits), "%02x", static_cast<unsigned char>(encrypted[i]));
        hex += digits;
        if (i % 32 == 31)
            hex += '\n';
    }
    std::string zeros;
    for (int i = 0; i < 8; ++i)
        zeros += std::string(64, '0') + "\n";
    const std::string program =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 400 100\n"
        "%!PS-AdobeFont-1.0: TestFont\n12 dict begin /FontName /TestFont def /FontType 1 def\n"
        "/FontMatrix [0.001 0 0 0.001 0 0] readonly def /Encoding StandardEncoding def\n"
        "/FontBBox {0 0 1000 1000} readonly def currentdict end currentfile eexec\n" +
        hex + "\n" + zeros + "cleartomark\n"
        "/TestFont findfont 10 scalefont setfont 0 80 moveto (AB) show (A) show\n"
        // Metrics in the font's dictionary win.
        "/TestFont findfont dup length dict copy dup /Metrics << /A 250 >> put /Metered exch definefont pop\n"
        "/Metered findfont 10 scalefont setfont 0 60 moveto (A) show (B) show\n"
        // A standard font of its own, and Latin-1 through a reencoding.
        "/Times-BoldItalic findfont 10 scalefont setfont 0 40 moveto (A) show (B) show\n"
        "/Times-Roman findfont dup length dict copy dup /Encoding ISOLatin1Encoding put /_Times-Roman exch definefont\n"
        "10 scalefont setfont 0 20 moveto (caf\\351) show (B) show\n"
        "0 0 10 10 rectfill\n";
    const double box[4] = {0, 0, 400, 100};
    const fla::vectors::Artwork art = fla::vectors::readPostScript(program, box);
    CHECK(art.notes.empty());
    for (const std::string& n : art.notes)
        std::printf("    note: %s\n", n.c_str());
    CHECK(art.texts.size() == 8);
    if (art.texts.size() == 8)
    {
        CHECK(art.texts[0].family == "Test Font" || art.texts[0].family == "TestFont");
        CHECK_NEAR(art.texts[1].placement.tx, 11);
        CHECK_NEAR(art.texts[3].placement.tx, 2.5);
        // Times-BoldItalic's A is 667.
        CHECK_NEAR(art.texts[5].placement.tx, 6.67);
        CHECK(art.texts[6].text == "caf\xC3\xA9" && art.texts[6].family == "Times");
        // c a f 444 444 333, é as e: 444.
        CHECK_NEAR(art.texts[7].placement.tx, 16.65);
    }
    // The program runs on past the font.
    CHECK(art.paths.size() == 1);
}
