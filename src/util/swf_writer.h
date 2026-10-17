#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// Writes SWF movies: shapes with solid, gradient and bitmap fills, and
/// lossless bitmaps, placed and removed frame by frame. Each shape has one
/// fill, filled even-odd, so a caller hands it outlines that do not cross
/// (simplified). Coordinates are in pixels; the file has them in twips.
///
/// And what a movie needs for its scripts: movie clips with timelines of
/// their own, buttons, text fields, placements with instance names, color
/// transforms, filters and blend modes, frame labels, and ActionScript as
/// action bytecode (DoAction, DoInitAction, clip and button actions) or as
/// ActionScript 3 (DoABC, SymbolClass). Free of Qt.
namespace swf {

struct Color
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

/// x' = a x + c y + tx, y' = b x + d y + ty, in pixels.
struct Matrix
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;
};

struct Fill
{
    enum class Type
    {
        Solid,
        Linear,
        Radial,
        Bitmap
    };
    Type type = Type::Solid;
    Color color;
    /// Gradients: offsets 0 to 1 and colors, at most 15; the placement of
    /// the gradient square (-819.2 to 819.2 pixels) in the shape; how it
    /// spreads (0 pad, 1 reflect, 2 repeat); a radial one's focal point,
    /// -1 to 1.
    struct Stop
    {
        double offset = 0.0;
        Color color;
    };
    std::vector<Stop> stops;
    Matrix matrix;
    int spread = 0;
    double focal = 0.0;
    /// Bitmaps: the bitmap's id, the placement of its pixels, whether it
    /// repeats past its edges and is smoothed.
    int bitmap = 0;
    bool repeat = false;
    bool smooth = true;
};

struct Segment
{
    enum class Kind
    {
        Move,
        Line,
        Quad
    };
    Kind kind = Kind::Line;
    /// The end, and for a quadratic curve its control.
    double x = 0.0, y = 0.0;
    double cx = 0.0, cy = 0.0;
};

/// How a StartSound plays its sound (SOUNDINFO): stopped, not again while
/// it plays (noMultiple), how many times round (0 for as many as SWF
/// allows, 65535), the part played and a volume envelope. In and out
/// points and envelope positions count samples at 44.1 kHz from the
/// sound's start; levels run 0 to 32768 (full).
struct SoundInfo
{
    bool stop = false;
    bool noMultiple = false;
    int loops = 1;
    /// 0 for from the start, or to the end.
    uint32_t inPoint = 0;
    uint32_t outPoint = 0;
    struct EnvelopePoint
    {
        uint32_t pos44 = 0;
        uint16_t left = 32768;
        uint16_t right = 32768;
    };
    std::vector<EnvelopePoint> envelope;
};

/// A color transform (CXFORMWITHALPHA): each channel multiplied, then the
/// offset (-255 to 255) added.
struct ColorTransform
{
    double red = 1.0, green = 1.0, blue = 1.0, alpha = 1.0;
    int redAdd = 0, greenAdd = 0, blueAdd = 0, alphaAdd = 0;

    bool identity() const
    {
        return red == 1.0 && green == 1.0 && blue == 1.0 && alpha == 1.0 && redAdd == 0 && greenAdd == 0 &&
            blueAdd == 0 && alphaAdd == 0;
    }
};

/// A filter on a placed movie clip or button, as PlaceObject3 lists them:
/// its type as SWF numbers them; its color (a bevel's shadow) and a bevel's
/// highlight; blur in pixels; a shadow's or bevel's angle in radians and
/// distance in pixels; strength (1 plain); inner, knockout and a bevel's
/// on top (full); passes (quality); a gradient filter's stops; a color
/// matrix's 20 numbers, row by row.
struct Filter
{
    enum class Type
    {
        DropShadow = 0,
        Blur = 1,
        Glow = 2,
        Bevel = 3,
        GradientGlow = 4,
        ColorMatrix = 6,
        GradientBevel = 7
    };
    Type type = Type::Blur;
    Color color;
    Color highlight{255, 255, 255, 255};
    double blurX = 4.0, blurY = 4.0;
    double angle = 0.0;
    double distance = 0.0;
    double strength = 1.0;
    bool inner = false;
    bool knockout = false;
    bool onTop = false;
    int passes = 1;
    struct Stop
    {
        double offset = 0.0;
        Color color;
    };
    std::vector<Stop> stops;
    float matrix[20] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
};

/// The events of a movie clip's handler (CLIPEVENTFLAGS), to or together.
namespace ClipEvent {
enum : uint32_t
{
    Load = 1u << 0,
    EnterFrame = 1u << 1,
    Unload = 1u << 2,
    MouseMove = 1u << 3,
    MouseDown = 1u << 4,
    MouseUp = 1u << 5,
    KeyDown = 1u << 6,
    KeyUp = 1u << 7,
    Data = 1u << 8,
    Initialize = 1u << 9,
    Press = 1u << 10,
    Release = 1u << 11,
    ReleaseOutside = 1u << 12,
    RollOver = 1u << 13,
    RollOut = 1u << 14,
    DragOver = 1u << 15,
    DragOut = 1u << 16,
    KeyPress = 1u << 17,
    Construct = 1u << 18
};
}

/// A movie clip's handler (CLIPACTIONRECORD): the events it runs on, the
/// key for KeyPress (a code from the key table, or a character), and its
/// actions, ActionEnd last.
struct ClipAction
{
    uint32_t events = 0;
    int key = 0;
    std::vector<uint8_t> actions;
};

/// A placement's CLIPACTIONS, for Placement::clipActions; empty for none.
std::vector<uint8_t> clipActions(const std::vector<ClipAction>& actions);

/// A character put at a depth, or a change to the one there
/// (PlaceObject2, or PlaceObject3 when it has filters, a blend mode or
/// bitmap caching).
struct Placement
{
    int depth = 1;
    /// The character placed, 0 to change the one at the depth (move).
    int character = 0;
    /// Changes or replaces what is at the depth, rather than placing anew.
    bool move = false;
    bool hasMatrix = false;
    Matrix matrix;
    bool hasColor = false;
    ColorTransform color;
    /// A morph shape's ratio, 0 to 65535; -1 for none.
    int ratio = -1;
    /// The instance name scripts address it by.
    std::string name;
    /// For a mask: the top depth it masks.
    int clipDepth = 0;
    bool hasFilters = false;
    std::vector<Filter> filters;
    /// The blend mode, as Writer::Blend numbers them (1 normal; 2 layer,
    /// 10 invert, 11 alpha, 12 erase too).
    bool hasBlend = false;
    int blend = 1;
    bool cacheAsBitmap = false;
    std::vector<uint8_t> clipActions;
};

/// One character of a button's states (BUTTONRECORD): which states show it
/// (1 up, 2 over, 4 down, 8 hit), at what depth and where, with its color,
/// filters and blend mode (0 for none).
struct ButtonRecord
{
    int states = 1;
    int character = 0;
    int depth = 1;
    Matrix matrix;
    ColorTransform color;
    std::vector<Filter> filters;
    int blend = 0;
};

/// The transitions a button's actions run on (BUTTONCONDACTION), to or
/// together; a key's code (ButtonCondition::key) for a key press.
namespace ButtonCondition {
enum : uint16_t
{
    IdleToOverUp = 0x0001,
    OverUpToIdle = 0x0002,
    OverUpToOverDown = 0x0004,
    OverDownToOverUp = 0x0008,
    OverDownToOutDown = 0x0010,
    OutDownToOverDown = 0x0020,
    OutDownToIdle = 0x0040,
    IdleToOverDown = 0x0080,
    OverDownToIdle = 0x0100
};
inline uint16_t key(int code)
{
    return static_cast<uint16_t>((code & 0x7F) << 9);
}
}

struct ButtonAction
{
    uint16_t conditions = 0;
    std::vector<uint8_t> actions;
};

/// A text field (DefineEditText): its box in pixels, the font (a
/// defineFont id) and its size in pixels, the color, the variable it shows
/// and the text it starts with, and what can be done with it.
struct EditText
{
    double left = 0.0, top = 0.0, right = 100.0, bottom = 20.0;
    int font = 0;
    double size = 12.0;
    Color color;
    std::string variable;
    std::string text;
    bool wordWrap = false;
    bool multiline = false;
    bool password = false;
    bool readOnly = false;
    bool noSelect = false;
    bool border = false;
    bool html = false;
    bool autoSize = false;
    bool useOutlines = false;
    int maxLength = 0;
    /// 0 left, 1 right, 2 center, 3 justify.
    int align = 0;
    double leftMargin = 0.0, rightMargin = 0.0, indent = 0.0, leading = 0.0;
};

/// A shape that changes from one form to another as its placement's ratio
/// runs from 0 to 65535 (DefineMorphShape): every fill and line with a
/// start and an end, and its outlines, each with the same pieces at the
/// start and at the end. Fills are solid or gradients (the end's stops as
/// many as the start's); lines solid.
struct MorphFill
{
    Fill start;
    Fill end;
};

struct MorphLine
{
    double startWidth = 1.0, endWidth = 1.0;
    Color startColor, endColor;
};

/// One outline of a morph shape: its fills on either side (as SWF's edges
/// have them: fill 0 on the left, fill 1 on the right) and its line, each
/// 1 and up into the morph's tables, 0 for none; and its pieces at the
/// start and at the end, a Move first and then as many Lines or Quads in
/// each.
struct MorphPath
{
    int fill0 = 0;
    int fill1 = 0;
    int line = 0;
    std::vector<Segment> start;
    std::vector<Segment> end;
};

/// A movie clip's own timeline (DefineSprite): characters placed and
/// removed, sounds started and scripts run frame by frame, as a movie's
/// are. Written with Writer::defineSprite.
class Sprite
{
public:
    void place(int id, int depth);
    void place(const Placement& placement);
    void remove(int depth);
    void startSound(int id, const SoundInfo& info);
    /// The frame's label (FrameLabel); an anchor names a place to link to.
    void frameLabel(const std::string& name, bool anchor = false);
    /// Actions run as the frame is entered (DoAction), ActionEnd last.
    void doAction(const std::vector<uint8_t>& actions);
    void showFrame();

    int frames() const { return _frames; }
    const std::vector<uint8_t>& tags() const { return _tags; }

private:
    int _frames = 0;
    std::vector<uint8_t> _tags;
};

class Writer
{
public:
    /// A movie of \a width by \a height pixels at \a fps on \a background.
    Writer(int width, int height, double fps, Color background);

    /// A bitmap of premultiplied ARGB pixels, row by row, four bytes each
    /// (alpha, red, green, blue); its character id.
    int defineBitmap(int width, int height, const std::vector<uint8_t>& argb);

    /// A shape filled with \a fill; its character id, or 0 when it has no
    /// outline.
    int defineShape(const Fill& fill, const std::vector<Segment>& outline);

    /// How a placed character mixes with what is under it, as SWF numbers
    /// them.
    enum class Blend
    {
        Normal = 1,
        Layer = 2,
        Multiply = 3,
        Screen = 4,
        Lighten = 5,
        Darken = 6,
        Difference = 7,
        Add = 8,
        Subtract = 9,
        Invert = 10,
        Alpha = 11,
        Erase = 12,
        Overlay = 13,
        HardLight = 14
    };

    /// Puts character \a id at \a depth, its alpha scaled by \a alpha,
    /// blending as \a blend says.
    void place(int id, int depth, double alpha = 1.0, Blend blend = Blend::Normal);
    void remove(int depth);

    /// Starts a soundtrack streamed with the movie (SoundStreamHead2): its
    /// codec, rate and channels; frame blocks follow with soundBlock.
    void soundStream(int codec, int rate, bool stereo, int samplesPerFrame, int latency = 0);
    /// This frame's part of the soundtrack (SoundStreamBlock), before
    /// showFrame; an empty block writes nothing.
    void soundBlock(const std::vector<uint8_t>& data);
    /// A sound the movie plays as an event (DefineSound): its codec (1
    /// ADPCM, 2 MP3, 3 raw), rate, channels, samples a channel, and data
    /// (for MP3, starting with its seek). Returns its id.
    int defineSound(int codec, int rate, bool stereo, uint32_t samples, const std::vector<uint8_t>& data);
    /// Starts sound \a id on this frame (StartSound): \a loops times round
    /// (at least once), not again while it plays with \a noMultiple, or
    /// stops it with \a stop.
    void startSound(int id, int loops, bool noMultiple, bool stop);
    /// Starts sound \a id on this frame as \a info says.
    void startSound(int id, const SoundInfo& info);

    /// A movie clip of \a sprite's frames (at least one); its character id.
    int defineSprite(const Sprite& sprite);

    /// A morph shape (DefineMorphShape); its character id, or 0 when it
    /// has no outline or an outline's start and end differ in pieces.
    int defineMorphShape(const std::vector<MorphFill>& fills, const std::vector<MorphLine>& lines,
        const std::vector<MorphPath>& paths);

    /// The sounds button \a button plays (DefineButtonSound), by transition:
    /// the pointer moving off (over up to idle), moving on (idle to over
    /// up), pressing (over up to over down) and releasing (over down to
    /// over up); a sound id 0 for none.
    void defineButtonSound(int button, const std::vector<std::pair<int, SoundInfo>>& transitions);

    /// A button (DefineButton2): its characters by state, and the actions
    /// its transitions run; its character id. A menu button tracks the
    /// pointer pressed elsewhere too.
    int defineButton(const std::vector<ButtonRecord>& records, const std::vector<ButtonAction>& actions,
        bool menu = false);
    /// A font named for the player to find among the system's (DefineFont2
    /// with no glyphs), for text fields; its id.
    int defineFont(const std::string& name, bool bold = false, bool italic = false);
    /// A text field; its character id.
    int defineEditText(const EditText& text);
    /// 9-slice scaling for movie clip \a sprite (DefineScalingGrid): the
    /// box in its own space, in pixels, whose corners keep their size.
    void defineScalingGrid(int sprite, double left, double top, double right, double bottom);

    /// Puts or changes a character at a depth.
    void place(const Placement& placement);
    /// This frame's label (FrameLabel); an anchor names a place to link to.
    void frameLabel(const std::string& name, bool anchor = false);
    /// Actions run as this frame is entered (DoAction), ActionEnd last.
    void doAction(const std::vector<uint8_t>& actions);
    /// Actions run once, before the first frame's, for movie clip \a sprite
    /// (DoInitAction): its #initclip code, or a class it defines.
    void doInitAction(int sprite, const std::vector<uint8_t>& actions);
    /// Characters scripts reach by name (ExportAssets): attachMovie's
    /// linkage identifiers.
    void exportAssets(const std::vector<std::pair<int, std::string>>& names);
    /// An ActionScript 3 program (DoABC): its ABC bytes, named, initialized
    /// when first used when \a lazy.
    void doABC(const std::string& name, const std::vector<uint8_t>& abc, bool lazy = true);
    /// The ActionScript 3 classes of characters (SymbolClass), 0 for the
    /// main timeline's (the document class).
    void symbolClass(const std::vector<std::pair<int, std::string>>& classes);
    /// Whether the movie runs ActionScript 3 rather than 1 and 2: the
    /// FileAttributes flag, and the SWF version the file says.
    void setActionScript3(bool on);

    void showFrame();

    /// The file: compressed (CWS) unless \a compress is false.
    std::vector<uint8_t> finish(bool compress = true);

    int frames() const { return _frames; }

    /// A tag this writer has no call for, its body as given.
    void tag(int code, const std::vector<uint8_t>& body);

private:

    int _width;
    int _height;
    double _fps;
    int _frames = 0;
    int _nextId = 1;
    std::vector<uint8_t> _tags;
    /// Where FileAttributes' flags are in the tags.
    size_t _attributesAt = 0;
};

} // namespace swf
