#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/play/display/backend.h"
#include "../src/play/display/bitmap_data.h"
#include "../src/play/display/display_object.h"
#include "../src/play/display/stage.h"
#include "../src/play/display/text_field.h"
#include "../src/parser/swf_import.h"
#include "../src/play/movie_runtime.h"
#include "../src/play/script/class_path.h"
#include "../src/util/png_writer.h"

#include "abc/abc.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// The flash.* classes ActionScript 3 movies use, played as Test Movie
// plays them.

namespace {

/// A movie built in memory, as ActionScript 3: a root timeline with frame
/// scripts, and symbols.
struct Movie
{
    std::unique_ptr<fla::Timeline> root{new fla::Timeline(nullptr)};
    std::vector<std::unique_ptr<fla::Symbol>> symbols;
    fla::Layer* actions = nullptr;

    Movie()
    {
        root->name = "Scene 1";
        actions = layer(*root, "actions");
    }

    fla::Layer* layer(fla::Timeline& timeline, const std::string& name)
    {
        fla::Layer* made = new fla::Layer(&timeline);
        made->name = name;
        timeline.layers.push_back(made);
        return made;
    }

    fla::Frame* frame(fla::Layer& on, int index, int duration, const std::string& script = std::string())
    {
        fla::Frame* made = new fla::Frame(&on);
        made->index = index;
        made->duration = duration;
        if (!script.empty())
        {
            made->actionScript = new fla::ActionScript(made);
            made->actionScript->code = script;
        }
        on.frames.push_back(made);
        return made;
    }

    /// A frame script on the root's actions layer.
    void script(int index, const std::string& code) { frame(*actions, index, 1, code); }

    fla::Symbol* symbol(const std::string& name, fla::SymbolType type, int frames)
    {
        auto made = std::make_unique<fla::Symbol>(nullptr);
        made->name = name;
        made->symbolType = type;
        fla::Timeline* timeline = new fla::Timeline(made.get());
        timeline->name = name;
        made->timelines.push_back(timeline);
        fla::Layer* content = layer(*timeline, "Layer 1");
        for (int i = 0; i < frames; ++i)
            frame(*content, i, 1);
        symbols.push_back(std::move(made));
        return symbols.back().get();
    }

    fla::SymbolInstance* place(fla::Frame& on, fla::Symbol& symbol, const std::string& name)
    {
        fla::SymbolInstance* made = new fla::SymbolInstance(&on);
        made->symbol = &symbol;
        made->libraryItemName = symbol.name;
        made->symbolType = symbol.symbolType;
        made->name = name;
        on.elements.push_back(made);
        return made;
    }
};

/// A runtime playing \a movie as ActionScript 3, what it traces and the
/// errors it tells of kept.
struct Played
{
    fla::play::MovieRuntime runtime;
    std::vector<std::string> traced;
    std::vector<std::string> errors;

    Played(Movie& movie, double frameRate = 24.0)
        : runtime(*movie.root, frameRate)
    {
        runtime.setActionScriptVersion(3);
        // Phoenix's own classes (fl.*), from the source tree as the app has
        // them built in.
        fla::play::ClassPath library;
        library.addRoot(std::filesystem::path(__FILE__).parent_path().parent_path() / "resources" / "as3");
        runtime.findClass = [library](const std::string& name) { return library.find(name); };
        runtime.trace = [this](const std::string& line) { traced.push_back(line); };
        runtime.error = [this](const std::string& line) { errors.push_back(line); };
    }

    /// Everything traced, a line each.
    std::string output() const
    {
        std::string all;
        for (const std::string& line : traced)
            all += line + "\n";
        return all;
    }

    void showErrors() const
    {
        for (const std::string& error : errors)
            std::printf("    %s\n", error.c_str());
    }
};

namespace fs = std::filesystem;

/// A folder of its own under the temporary folder, empty.
fs::path freshFolder(const char* name)
{
    const fs::path folder = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder);
    return folder;
}

void writeFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

/// Draws the solid fills of Shapes and Sprites into pixels, enough for a
/// test of BitmapData.draw: each pixel whose center is in a fill (even-odd)
/// takes its color.
class FillBackend : public fla::display::Backend
{
public:
    void draw(fla::display::BitmapData& target, const fla::display::DisplayObject& source,
        const fla::display::Matrix& matrix, const fla::display::ColorTransform*, fla::display::BlendMode,
        const fla::display::Rect*, bool) override
    {
        const fla::display::Graphics* graphics = nullptr;
        if (source.kind() == fla::display::ObjectKind::Shape)
            graphics = &static_cast<const fla::display::Shape&>(source).graphics;
        else if (source.kind() == fla::display::ObjectKind::Sprite || source.kind() == fla::display::ObjectKind::MovieClip)
            graphics = &static_cast<const fla::display::Sprite&>(source).graphics;
        if (graphics)
            for (const fla::display::GraphicsPath& path : graphics->paths())
            {
                if (path.kind != fla::display::GraphicsPath::Kind::Fill ||
                    path.fill.type != fla::display::FillStyle::Type::Solid)
                    continue;
                std::vector<std::vector<fla::display::Point>> polygons = fla::display::flatten(path.steps);
                for (auto& polygon : polygons)
                    for (auto& point : polygon)
                        point = matrix.map(point);
                for (int y = 0; y < target.height(); ++y)
                    for (int x = 0; x < target.width(); ++x)
                    {
                        bool inside = false;
                        for (const auto& polygon : polygons)
                            for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
                            {
                                const auto& a = polygon[i];
                                const auto& b = polygon[j];
                                if ((a.y > y + 0.5) != (b.y > y + 0.5) &&
                                    x + 0.5 < (b.x - a.x) * (y + 0.5 - a.y) / (b.y - a.y) + a.x)
                                    inside = !inside;
                            }
                        if (inside)
                            target.setPixel32(x, y,
                                (static_cast<uint32_t>(path.fill.alpha * 255.0 + 0.5) << 24) | (path.fill.color & 0xffffff));
                    }
            }
        if (source.isContainer())
            for (const auto& child : static_cast<const fla::display::DisplayObjectContainer&>(source).children())
                if (child)
                    draw(target, *child, child->matrix().then(matrix), nullptr, fla::display::BlendMode::Normal, nullptr,
                        false);
    }
    bool applyFilter(fla::display::BitmapData&, const fla::display::BitmapData&, const fla::display::Rect&,
        const fla::display::Point&, const fla::display::BitmapFilter&) override
    {
        return false;
    }
    fla::display::Rect artBounds(const fla::display::TimelineArt&, bool) override { return {}; }
    bool artHits(const fla::display::TimelineArt&, double, double) override { return false; }
    std::vector<fla::display::TextLine> layoutText(const fla::display::TextField&) override { return {}; }
    int textIndexAt(const fla::display::TextField&, double, double) override { return 0; }
    fla::display::Rect textCharBox(const fla::display::TextField&, int) override { return {}; }
};

} // namespace

TEST(as3_geometry_does_the_math)
{
    Movie movie;
    movie.script(0,
        "import flash.geom.*;\n"
        "stop();\n"
        "var p:Point = new Point(3, 4);\n"
        "trace(p.length, p.add(new Point(1, 1)), p.subtract(new Point(1, 1)), Point.distance(p, new Point()));\n"
        "trace(Point.interpolate(new Point(10, 10), new Point(0, 0), 0.25), Point.polar(2, 0));\n"
        "p.normalize(10); trace(p, p.equals(new Point(6, 8)), p.clone() == p);\n"
        "var r:Rectangle = new Rectangle(0, 0, 10, 10);\n"
        "var s:Rectangle = new Rectangle(5, 5, 10, 10);\n"
        "trace(r.intersection(s), r.union(s), r.intersects(s), r.containsRect(new Rectangle(1, 1, 2, 2)));\n"
        "trace(r.contains(9.5, 9.5), r.contains(10, 10), r.containsPoint(new Point(2, 2)), r.bottomRight, r.size);\n"
        "r.inflate(1, 2); trace(r, r.isEmpty()); r.setEmpty(); trace(r.isEmpty(), r);\n"
        "r.offset(3, 4); r.right = 10; trace(r.left, r.width, r.topLeft);\n"
        "var m:Matrix = new Matrix();\n"
        "m.scale(2, 3); m.translate(10, 20);\n"
        "trace(m, m.transformPoint(new Point(1, 1)), m.deltaTransformPoint(new Point(1, 1)));\n"
        "var n:Matrix = m.clone(); n.invert(); trace(n.transformPoint(new Point(12, 23)));\n"
        "var box:Matrix = new Matrix(); box.createBox(2, 2, 0, 5, 5); trace(box);\n"
        "var a:Matrix = new Matrix(1, 0, 0, 1, 5, 0); a.concat(new Matrix(2, 0, 0, 2, 0, 0)); trace(a.tx, a.a);\n"
        "var c:ColorTransform = new ColorTransform(); c.color = 0xFF8000;\n"
        "trace(c.redOffset, c.greenOffset, c.redMultiplier, c.color.toString(16));\n"
        "var half:ColorTransform = new ColorTransform(0.5, 0.5, 0.5, 1, 10, 0, 0, 0);\n"
        "half.concat(new ColorTransform(1, 1, 1, 1, 20, 0, 0, 0)); trace(half.redMultiplier, half.redOffset);\n");
    Played played(movie);
    played.runtime.start();
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected =
        "5 (x=4, y=5) (x=2, y=3) 5\n"
        "(x=2.5, y=2.5) (x=2, y=0)\n"
        "(x=6, y=8) true false\n"
        "(x=5, y=5, w=5, h=5) (x=0, y=0, w=15, h=15) true true\n"
        "true false true (x=10, y=10) (x=10, y=10)\n"
        "(x=-1, y=-2, w=12, h=14) false\n"
        "true (x=0, y=0, w=0, h=0)\n"
        "3 7 (x=3, y=4)\n"
        "(a=2, b=0, c=0, d=3, tx=10, ty=20) (x=12, y=23) (x=2, y=3)\n"
        "(x=1, y=1)\n"
        "(a=2, b=0, c=0, d=2, tx=5, ty=5)\n"
        "10 2\n"
        "255 128 0 ff8000\n"
        "0.5 20\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}

TEST(as3_events_capture_bubble_and_script_dispatchers)
{
    Movie movie;
    movie.script(0,
        "import flash.display.Sprite;\n"
        "import flash.events.*;\n"
        "stop();\n"
        "var outer:Sprite = new Sprite(); var inner:Sprite = new Sprite();\n"
        "outer.name = 'outer'; inner.name = 'inner';\n"
        "outer.addChild(inner); addChild(outer);\n"
        "var log:Array = [];\n"
        "function note(e:Event):void { log.push(e.currentTarget.name + ':' + e.eventPhase); }\n"
        "outer.addEventListener('ping', note, true);\n"
        "outer.addEventListener('ping', note);\n"
        "inner.addEventListener('ping', note);\n"
        "inner.dispatchEvent(new Event('ping', true));\n"
        "trace(log.join(' '));\n"
        "log = [];\n"
        "inner.addEventListener('ping', function(e:Event):void { e.stopPropagation(); }, false, -1);\n"
        "inner.dispatchEvent(new Event('ping', true));\n"
        "trace(log.join(' '));\n"
        "var e:Event = new Event(Event.CHANGE, false, true);\n"
        "trace(e.type, e.bubbles, e.cancelable, e.isDefaultPrevented(), e.target);\n"
        "e.preventDefault(); trace(e.isDefaultPrevented(), e.clone().type, e);\n"
        "var m:MouseEvent = new MouseEvent(MouseEvent.CLICK);\n"
        "trace(m.bubbles, m.type, m is Event, m.shiftKey);\n");
    Played played(movie);
    played.runtime.setClasses(
        {{"menus.Menu",
             "package menus {\n"
             "  import flash.events.Event;\n"
             "  import flash.events.EventDispatcher;\n"
             "  public class Menu extends EventDispatcher {\n"
             "    public var chosen:String = '';\n"
             "    public function choose(item:String):void { chosen = item; dispatchEvent(new Event(Event.CHANGE)); }\n"
             "  }\n"
             "}\n"},
            {"menus.Picked",
                "package menus {\n"
                "  import flash.events.Event;\n"
                "  public class Picked extends Event {\n"
                "    public var item:String;\n"
                "    public function Picked(item:String) { super('picked', true); this.item = item; }\n"
                "    override public function clone():Event { return new Picked(item); }\n"
                "  }\n"
                "}\n"}},
        std::string());
    played.runtime.start();
    played.showErrors();
    CHECK(played.errors.empty());
    CHECK(played.traced.size() == 5);
    if (played.traced.size() == 5)
    {
        CHECK(played.traced[0] == "outer:1 inner:2 outer:3");
        CHECK(played.traced[1] == "outer:1 inner:2");
        CHECK(played.traced[2] == "change false true false null");
        CHECK(played.traced[3] == "true change [Event type=\"change\" bubbles=false cancelable=true eventPhase=0]");
        CHECK(played.traced[4] == "true click true false");
    }

    // A script class extending EventDispatcher, and one extending Event
    // sent again (as its clone) from a listener.
    Movie second;
    second.script(0,
        "import menus.*;\n"
        "import flash.events.Event;\n"
        "import flash.display.Sprite;\n"
        "stop();\n"
        "var menu:Menu = new Menu();\n"
        "menu.addEventListener(Event.CHANGE, function(e:Event):void { trace('chose ' + e.target.chosen, e.target == menu); });\n"
        "menu.choose('paint');\n"
        "var holder:Sprite = new Sprite(); var child:Sprite = new Sprite(); holder.addChild(child);\n"
        "var again:Sprite = new Sprite();\n"
        "holder.addEventListener('picked', function(e:Picked):void { trace('picked ' + e.item, e is Picked); again.dispatchEvent(e); });\n"
        "again.addEventListener('picked', function(e:Picked):void { trace('again ' + e.item, e.target == again); });\n"
        "child.dispatchEvent(new Picked('brush'));\n"
        "trace(menu.hasEventListener(Event.CHANGE), menu.willTrigger('nothing'));\n");
    Played secondPlayed(second);
    secondPlayed.runtime.setClasses(
        {{"menus.Menu",
             "package menus {\n"
             "  import flash.events.Event;\n"
             "  import flash.events.EventDispatcher;\n"
             "  public class Menu extends EventDispatcher {\n"
             "    public var chosen:String = '';\n"
             "    public function choose(item:String):void { chosen = item; dispatchEvent(new Event(Event.CHANGE)); }\n"
             "  }\n"
             "}\n"},
            {"menus.Picked",
                "package menus {\n"
                "  import flash.events.Event;\n"
                "  public class Picked extends Event {\n"
                "    public var item:String;\n"
                "    public function Picked(item:String) { super('picked', true); this.item = item; }\n"
                "    override public function clone():Event { return new Picked(item); }\n"
                "  }\n"
                "}\n"}},
        std::string());
    secondPlayed.runtime.start();
    secondPlayed.showErrors();
    CHECK(secondPlayed.errors.empty());
    CHECK(secondPlayed.output() == "chose paint true\npicked brush true\nagain brush true\ntrue false\n");
    if (secondPlayed.output() != "chose paint true\npicked brush true\nagain brush true\ntrue false\n")
        std::printf("%s", secondPlayed.output().c_str());
}

TEST(as3_sprite_subclass_draws_into_a_bitmap)
{
    FillBackend backend;
    fla::display::Backend* was = fla::display::backend();
    fla::display::setBackend(&backend);
    Movie movie;
    movie.script(0,
        "import art.Swatch;\n"
        "import flash.display.*;\n"
        "import flash.geom.*;\n"
        "stop();\n"
        "var swatch:Swatch = new Swatch(0xFF0000);\n"
        "addChild(swatch);\n"
        "var pixels:BitmapData = new BitmapData(20, 20, true, 0);\n"
        "pixels.draw(swatch, new Matrix(1, 0, 0, 1, 5, 5));\n"
        "trace(pixels.getPixel32(10, 10).toString(16), pixels.getPixel32(2, 2), pixels.getPixel(14, 14).toString(16));\n"
        "trace(swatch is Sprite, swatch is DisplayObject, swatch.width, swatch.parent == this, swatch.kind);\n"
        "var copy:BitmapData = pixels.clone(); copy.fillRect(new Rectangle(0, 0, 2, 2), 0xFF00FF00);\n"
        "trace(copy.getPixel(0, 0).toString(16), pixels.getPixel(0, 0), copy.width, copy.rect);\n"
        "var shown:Bitmap = new Bitmap(pixels); addChild(shown); trace(shown.width, shown.bitmapData.getPixel(10, 10).toString(16));\n");
    Played played(movie);
    played.runtime.setClasses({{"art.Swatch",
                                  "package art {\n"
                                  "  import flash.display.Sprite;\n"
                                  "  public class Swatch extends Sprite {\n"
                                  "    public var kind:String = 'swatch';\n"
                                  "    public function Swatch(color:uint) {\n"
                                  "      graphics.beginFill(color);\n"
                                  "      graphics.drawRect(0, 0, 10, 10);\n"
                                  "      graphics.endFill();\n"
                                  "    }\n"
                                  "  }\n"
                                  "}\n"}},
        std::string());
    played.runtime.start();
    fla::display::setBackend(was);
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "ffff0000 0 ff0000\ntrue true 10 true swatch\nff00 0 20 (x=0, y=0, w=20, h=20)\n20 ff0000\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}

TEST(as3_byte_array_round_trips)
{
    Movie movie;
    movie.script(0,
        "import flash.utils.*;\n"
        "stop();\n"
        "var b:ByteArray = new ByteArray();\n"
        "b.writeInt(-2); b.writeUnsignedInt(0xFFFFFFFE); b.writeShort(-3); b.writeByte(200); b.writeBoolean(true);\n"
        "b.writeFloat(1.5); b.writeDouble(Math.PI); b.writeUTF('héllo'); b.writeUTFBytes('ok');\n"
        "trace(b.length, b.position, b.bytesAvailable);\n"
        "b.position = 0;\n"
        "trace(b.readInt(), b.readUnsignedInt(), b.readShort(), b.readByte(), b.readBoolean(), b.readFloat(), b.readDouble() == Math.PI, b.readUTF(), b.readUTFBytes(2));\n"
        "var l:ByteArray = new ByteArray(); l.endian = Endian.LITTLE_ENDIAN; l.writeInt(1);\n"
        "trace(l[0], l[3], l.endian);\n"
        "var big:ByteArray = new ByteArray(); for (var i:int = 0; i < 1000; i++) big.writeByte(i % 7);\n"
        "big.compress(); var small:uint = big.length; big.uncompress();\n"
        "trace(small < 1000, big.length, big[999], big.position);\n"
        "var part:ByteArray = new ByteArray(); big.position = 10; big.readBytes(part, 0, 5);\n"
        "trace(part.length, part[0], part[4]);\n"
        "var joined:ByteArray = new ByteArray(); joined.writeBytes(part, 1, 2); trace(joined.length, joined[0]);\n"
        "try { joined.position = 0; joined.readInt(); } catch (e:Error) { trace(e is EOFError, e.errorID); }\n"
        "b.clear(); trace(b.length, b.position);\n");
    Played played(movie);
    played.runtime.start();
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "34 34 0\n"
                                 "-2 4294967294 -3 -56 true 1.5 true héllo ok\n"
                                 "1 0 littleEndian\n"
                                 "true 1000 5 0\n"
                                 "5 3 0\n"
                                 "2 4\n"
                                 "true 2030\n"
                                 "0 0\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}

TEST(as3_byte_array_writes_objects_in_amf)
{
    Movie movie;
    movie.script(0,
        "import flash.utils.*;\nimport flash.net.*;\n"
        "stop();\n"
        "var b:ByteArray = new ByteArray();\n"
        "var o:Object = {first: 'Hello'};\n"
        "b.writeObject(o);\n"
        "var bytes:Array = []; for (var i:int = 0; i < b.length; i++) bytes.push(b[i]);\n"
        "trace(bytes);\n"
        "b.position = 0; var back:Object = b.readObject(); trace(back.first);\n"
        "var list:Array = [1, 'two', 3.5, null, true, new Date(1000)];\n"
        "var c:ByteArray = new ByteArray(); c.writeObject([list, list]); c.position = 0;\n"
        "var both:Array = c.readObject(); trace(both[0] === both[1], both[0][1], both[0][2], both[0][5].getTime());\n"
        "ByteArray.defaultObjectEncoding = ObjectEncoding.AMF0;\n"
        "var d:ByteArray = new ByteArray(); trace(d.objectEncoding); d.writeObject({n: 4}); trace(d[0], d.length);\n"
        "d.position = 0; trace(d.readObject().n);\n");
    Played played(movie);
    played.runtime.start();
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "10,11,1,11,102,105,114,115,116,6,11,72,101,108,108,111,1\n"
                                 "Hello\n"
                                 "true two 3.5 1000\n"
                                 "0\n"
                                 "3 16\n"
                                 "4\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}

TEST(as3_shared_objects_keep_their_values_in_sol_files)
{
    // Kept as Flash Player keeps them, a Date stays a Date.
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "phoenix-test-shared-objects";
    std::error_code error;
    fs::remove_all(folder, error);
    const auto run = [&](const std::string& script) {
        Movie movie;
        movie.script(0, "import flash.net.*;\nstop();\n" + script);
        Played played(movie);
        played.runtime.sharedObjectFolder = folder.u8string();
        played.runtime.start();
        played.showErrors();
        CHECK(played.errors.empty());
        return played.output();
    };
    const std::string first = run("var so:SharedObject = SharedObject.getLocal('game');\n"
                                  "trace(so.size);\n"
                                  "so.data.level = 3; so.data.when = new Date(5000); so.data.names = ['a', 'b'];\n"
                                  "trace(so.size > 0, so.flush());\n");
    CHECK(first == "0\ntrue flushed\n");
    const std::string second = run("var so:SharedObject = SharedObject.getLocal('game');\n"
                                   "trace(so.data.level, so.data.when is Date, so.data.when.getTime(), so.data.names);\n");
    CHECK(second == "3 true 5000 a,b\n");
    if (second != "3 true 5000 a,b\n")
        std::printf("%s%s", first.c_str(), second.c_str());
    fs::remove_all(folder, error);
}

TEST(as3_timers_fire_as_the_movie_steps)
{
    Movie movie;
    movie.frame(*movie.actions, 0, 20,
        "import flash.utils.*;\n"
        "import flash.events.*;\n"
        "var ticks:int = 0;\n"
        "var timer:Timer = new Timer(100, 3);\n"
        "timer.addEventListener(TimerEvent.TIMER, function(e:TimerEvent):void { ticks++; trace('tick ' + timer.currentCount); });\n"
        "timer.addEventListener(TimerEvent.TIMER_COMPLETE, function(e:TimerEvent):void { trace('done', timer.running); });\n"
        "timer.start();\n"
        "setTimeout(function():void { trace('timeout'); }, 250);\n"
        "var frames:int = 0;\n"
        "addEventListener(Event.ENTER_FRAME, function(e:Event):void { frames++; });\n");
    Played played(movie, 10.0);
    played.runtime.start();
    for (int i = 0; i < 6; ++i)
        played.runtime.step();
    played.showErrors();
    CHECK(played.errors.empty());
    CHECK(played.output() == "tick 1\ntick 2\ntimeout\ntick 3\ndone false\n");
    if (played.output() != "tick 1\ntick 2\ntimeout\ntick 3\ndone false\n")
        std::printf("%s", played.output().c_str());
}

TEST(as3_url_loader_and_loader_read_local_files)
{
    const fs::path folder = freshFolder("phoenix_as3_loading");
    writeFile(folder / "data" / "words.txt", "hello=world&count=3");
    // A 2 by 2 PNG: red, green, blue and see-through white.
    const std::vector<uint8_t> rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0};
    const std::vector<uint8_t> png = png::encodeRgba(2, 2, rgba);
    {
        std::ofstream out(folder / "dot.png", std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    Movie movie;
    movie.frame(*movie.actions, 0, 10,
        "import flash.net.*;\n"
        "import flash.events.*;\n"
        "import flash.display.*;\n"
        "var text:URLLoader = new URLLoader();\n"
        "text.addEventListener(Event.COMPLETE, function(e:Event):void { trace('text ' + e.target.data); });\n"
        "text.load(new URLRequest('data/words.txt'));\n"
        "var vars:URLLoader = new URLLoader(); vars.dataFormat = URLLoaderDataFormat.VARIABLES;\n"
        "vars.addEventListener(Event.COMPLETE, function(e:Event):void { trace('vars ' + vars.data.hello + ' ' + vars.data.count); });\n"
        "vars.load(new URLRequest('data/words.txt'));\n"
        "var missing:URLLoader = new URLLoader();\n"
        "missing.addEventListener(IOErrorEvent.IO_ERROR, function(e:IOErrorEvent):void { trace('missing ' + e.errorID); });\n"
        "missing.load(new URLRequest('nothing.txt'));\n"
        "var loader:Loader = new Loader();\n"
        "loader.contentLoaderInfo.addEventListener(Event.COMPLETE, function(e:Event):void {\n"
        "  var shown:Bitmap = loader.content as Bitmap;\n"
        "  trace('image', loader.contentLoaderInfo.width, loader.contentLoaderInfo.height, shown.bitmapData.getPixel32(0, 0).toString(16),\n"
        "    shown.bitmapData.getPixel32(1, 1), e.currentTarget.loader == loader, e.currentTarget.content == shown);\n"
        "});\n"
        "loader.load(new URLRequest('dot.png'));\n"
        "addChild(loader);\n"
        "var u:URLVariables = new URLVariables('a=1&b=two%20words'); trace(u.a, u.b);\n");
    Played played(movie);
    played.runtime.folder = folder.u8string();
    played.runtime.start();
    played.runtime.step();
    played.showErrors();
    CHECK(played.errors.empty());
    CHECK(played.output() ==
        "1 two words\ntext hello=world&count=3\nvars world 3\nmissing 2032\nimage 2 2 ffff0000 0 true true\n");
    if (played.output() !=
        "1 two words\ntext hello=world&count=3\nvars world 3\nmissing 2032\nimage 2 2 ffff0000 0 true true\n")
        std::printf("%s", played.output().c_str());
    std::error_code error;
    fs::remove_all(folder, error);
}

TEST(as3_library_symbols_are_classes)
{
    Movie movie;
    fla::Symbol* close = movie.symbol("Close", fla::SymbolType::MovieClip, 3);
    close->extraAttributes.emplace_back("linkageClassName", "CloseButton");
    fla::Symbol* star = movie.symbol("Star", fla::SymbolType::MovieClip, 2);
    star->extraAttributes.emplace_back("linkageClassName", "game.Star");
    fla::Symbol* panel = movie.symbol("Panel", fla::SymbolType::MovieClip, 1);
    panel->extraAttributes.emplace_back("linkageClassName", "game.Panel");
    // The panel holds a star named "twinkle"; one more on the stage.
    movie.place(*panel->timelines[0]->layers[0]->frames[0], *star, "twinkle");
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 2), *panel, "panel");
    movie.script(0,
        "import flash.utils.getDefinitionByName;\n"
        "import flash.utils.getQualifiedClassName;\n"
        "import flash.display.MovieClip;\n"
        "import game.*;\n"
        "trace('frame 1', panel is Panel, panel.twinkle is Star, panel.made);\n"
        "var Made:Class = getDefinitionByName('CloseButton') as Class;\n"
        "var c:MovieClip = new Made();\n"
        "addChild(c);\n"
        "trace(c is MovieClip, c.totalFrames, getQualifiedClassName(c), getQualifiedClassName(panel.twinkle));\n"
        "var s:Star = new Star(); trace(s.totalFrames, s.lit);\n");
    movie.script(1, "stop();\ntrace('frame 2', panel.twinkle.currentFrame);\n");
    Played played(movie);
    played.runtime.setLibrary({close, star, panel}, {}, {});
    played.runtime.setClasses({{"game.Star",
                                  "package game {\n"
                                  "  import flash.display.MovieClip;\n"
                                  "  public class Star extends MovieClip {\n"
                                  "    public var lit:Boolean;\n"
                                  "    public function Star() { lit = true; stop(); }\n"
                                  "  }\n"
                                  "}\n"},
                                 {"game.Panel",
                                     "package game {\n"
                                     "  import flash.display.MovieClip;\n"
                                     "  public class Panel extends MovieClip {\n"
                                     "    public var twinkle:Star;\n"
                                     "    public var made:String;\n"
                                     "    public function Panel() { made = twinkle ? 'with star' : 'no star'; }\n"
                                     "  }\n"
                                     "}\n"}},
        std::string());
    played.runtime.start();
    played.runtime.step();
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "frame 1 true true with star\ntrue 3 CloseButton game::Star\n2 true\nframe 2 1\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
    // The clip made with new is on the stage, and plays there.
    CHECK(played.runtime.madeChildren(played.runtime.root()).size() == 1);
}

TEST(as3_class_path_finds_classes_by_package)
{
    const fs::path folder = freshFolder("phoenix_as3_class_path");
    writeFile(folder / "game" / "util" / "Helper.as",
        "package game.util {\n"
        "  public class Helper {\n"
        "    public static function twice(n:int):int { return n * 2; }\n"
        "  }\n"
        "}\n");
    // Sources kept with a library beside the movie: lib/x.swc and src/.
    writeFile(folder / "corelib" / "src" / "com" / "lib" / "Tool.as",
        "package com.lib {\n"
        "  import game.util.Helper;\n"
        "  public class Tool { public function use():int { return Helper.twice(5); } }\n"
        "}\n");
    fla::play::ClassPath path;
    path.addRoot(folder);
    path.addRoot(folder / "corelib" / "src");
    Movie movie;
    movie.script(0,
        "import game.util.Helper;\n"
        "import com.lib.Tool;\n"
        "stop();\n"
        "trace(Helper.twice(21), new Tool().use());\n"
        "trace(URLLoader != null, ByteArray != null, navigateToURL != null);\n");
    Played played(movie);
    played.runtime.findClass = [path](const std::string& name) { return path.find(name); };
    played.runtime.start();
    played.showErrors();
    CHECK(played.errors.empty());
    CHECK(played.output() == "42 10\ntrue true true\n");
    if (played.output() != "42 10\ntrue true true\n")
        std::printf("%s", played.output().c_str());
    std::error_code error;
    fs::remove_all(folder, error);
}

TEST(as3_frame_scripts_share_the_timeline)
{
    Movie movie;
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 1);
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 3), *hat, "hat");
    movie.script(0,
        "var count:int = 1;\n"
        "function hello(who:String):String { return 'hello ' + who + ' ' + count + ' ' + (this == root); }\n"
        "hat.x = 5;\n");
    movie.script(1, "count++;\ntrace(hello('again'), hat.x, this.hat == hat);\n");
    movie.script(2, "stop();\ntrace(getChildByName('hat') == hat, typeof hello, broken());\n");
    Played played(movie);
    played.runtime.start();
    played.runtime.step();
    played.runtime.step();
    // An error stops its script and is told, with where it was; the movie
    // goes on.
    CHECK(played.output() == "hello again 2 true 5 true\n");
    if (played.output() != "hello again 2 true 5 true\n")
        std::printf("%s", played.output().c_str());
    CHECK(played.errors.size() == 1 && played.errors[0].find("frame 3") != std::string::npos &&
        played.errors[0].find("broken") != std::string::npos);
    CHECK(!played.runtime.root().playing && played.runtime.root().frame == 2);
}

TEST(as3_filters_and_adjust_color)
{
    Movie movie;
    movie.script(0,
        "import flash.filters.*;\n"
        "import flash.display.*;\n"
        "import fl.motion.AdjustColor;\n"
        "stop();\n"
        "var s:Sprite = new Sprite(); addChild(s);\n"
        "var blur:BlurFilter = new BlurFilter(8, 2, BitmapFilterQuality.HIGH);\n"
        "s.filters = [blur, new DropShadowFilter(5, 90, 0xFF0000, 0.5)];\n"
        "blur.blurX = 100;\n"
        "var got:Array = s.filters;\n"
        "trace(got.length, got[0].blurX, got[0].quality, got[1].distance, got[1].color.toString(16), got[0] is BlurFilter);\n"
        "got[0].blurX = 1; trace(s.filters[0].blurX);\n"
        "var a:AdjustColor = new AdjustColor();\n"
        "a.brightness = 0; a.contrast = 0; a.saturation = 0;\n"
        "trace(a.CalculateFinalFlatArray());\n"
        "a.hue = 0; trace(a.CalculateFinalFlatArray());\n"
        "a.brightness = 50; trace(a.CalculateFinalFlatArray().slice(0, 5));\n"
        "var m:ColorMatrixFilter = new ColorMatrixFilter(); m.matrix = a.CalculateFinalFlatArray(); trace(m.matrix[4]);\n");
    Played played(movie);
    played.runtime.start();
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "2 8 3 5 ff0000 true\n"
                                 "8\n"
                                 "null\n"
                                 "1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0\n"
                                 "1,0,0,0,50\n"
                                 "50\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}

TEST(as3_listeners_hear_the_stage_pointer_and_keys)
{
    Movie movie;
    movie.script(0,
        "import flash.display.*;\n"
        "import flash.events.*;\n"
        "stop();\n"
        "var box:Sprite = new Sprite(); box.graphics.beginFill(0); box.graphics.drawRect(0, 0, 50, 50);\n"
        "box.x = 100; box.y = 100; box.name = 'box'; addChild(box);\n"
        "box.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void {\n"
        "  trace('click', e.localX, e.localY, e.stageX, e.target.name); e.updateAfterEvent(); });\n"
        "stage.addEventListener(MouseEvent.MOUSE_DOWN, function(e:MouseEvent):void { trace('down on stage', e.target == box); });\n"
        "stage.addEventListener(KeyboardEvent.KEY_DOWN, function(e:KeyboardEvent):void { trace('key', e.keyCode, e.charCode); });\n"
        "trace(stage.stageWidth, stage.frameRate, stage == box.stage, root == this);\n");
    Played played(movie);
    played.runtime.setStagePointer(true);
    played.runtime.start();
    fla::display::Stage& stage = played.runtime.stage();
    stage.pointerMove(110, 120);
    stage.pointerDown(110, 120);
    stage.pointerUp(110, 120);
    played.runtime.afterEvents();
    played.runtime.keyDown(65, 'a');
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "550 24 true true\ndown on stage true\nclick 10 20 110 box\nkey 65 97\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}

TEST(as3_compiled_scripts_run_from_a_published_movie)
{
    // A movie as one is published: its main timeline's class and a symbol's
    // class compiled to ABC (with libas3's compiler, as a player's movie
    // has them), in a DoABC tag, the main timeline's class in SymbolClass.
    as3::abc::Timeline main;
    main.className = as3::abc::timelineClassName("movie");
    main.instances = {{"hat", "flash.display.MovieClip"}};
    main.frames = {{0,
                       "import game.Star;\n"
                       "import fl.text.RuntimeManager;\n"
                       "import flash.geom.Rectangle;\n"
                       "var star:Star = new Star();\n"
                       "addChild(star);\n"
                       "trace('frame 1', hat != null, star.lit, star.totalFrames, star is MovieClip);\n"
                       "RuntimeManager.getSingleton().addInstance(this, 'label', new Rectangle(5, 5, 90, 20),\n"
                       "  new XML('<tlfTextObject type=\"Point\"><TextFlow><p><span color=\"#ff0000\" fontSize=\"14\">'\n"
                       "  + 'Hi there</span></p></TextFlow></tlfTextObject>'), null, undefined, 0, 1);\n"
                       "trace(TextField(getChildByName('label')).text);\n"},
        {1, "stop();\ntrace('frame 2', star.currentFrame);\n"}};
    const as3::abc::Output compiled = as3::abc::compile(
        {{"package game {\n"
          "  import flash.display.MovieClip;\n"
          "  public class Star extends MovieClip {\n"
          "    public var lit:Boolean;\n"
          "    public function Star() { lit = true; stop(); }\n"
          "  }\n"
          "}\n",
            "Star.as"}},
        {main});
    CHECK(compiled.ok);
    if (!compiled.ok)
    {
        std::printf("%s", compiled.errorText().c_str());
        return;
    }

    // The movie: DoABC, SymbolClass naming the main timeline's class, and
    // one frame.
    std::vector<uint8_t> tags;
    const auto tag = [&tags](int code, const std::vector<uint8_t>& body) {
        const uint16_t header = static_cast<uint16_t>(code << 6 | 0x3F);
        tags.push_back(header & 0xFF);
        tags.push_back(header >> 8);
        for (int i = 0; i < 4; ++i)
            tags.push_back(static_cast<uint8_t>(body.size() >> (8 * i)));
        tags.insert(tags.end(), body.begin(), body.end());
    };
    tag(69, {0x08, 0, 0, 0}); // FileAttributes: ActionScript 3
    std::vector<uint8_t> doAbc = {1, 0, 0, 0, 0}; // lazy, no name
    doAbc.insert(doAbc.end(), compiled.abc.begin(), compiled.abc.end());
    tag(82, doAbc);
    std::vector<uint8_t> symbols = {1, 0, 0, 0};
    for (char c : main.className)
        symbols.push_back(static_cast<uint8_t>(c));
    symbols.push_back(0);
    tag(76, symbols);
    tag(1, {});
    tag(0, {});
    // A 550 by 400 stage (a RECT of 15-bit values), 24 frames a second.
    std::vector<uint8_t> swf = {'F', 'W', 'S', 10, 0, 0, 0, 0, 0x78, 0x00, 0x05, 0x5F, 0x00, 0x00, 0x0F, 0xA0, 0x00,
        0x00, 0x18, 0x01, 0x00};
    swf.insert(swf.end(), tags.begin(), tags.end());
    for (int i = 0; i < 4; ++i)
        swf[4 + i] = static_cast<uint8_t>(swf.size() >> (8 * i));

    // Imported, it keeps the ABC and says to run it with that class.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(swf, files, error));
    CHECK(files.count("bin/abc/1.abc") == 1);
    CHECK(files["bin/abc/1.abc"] == compiled.abc);
    const std::string settings(files["PublishSettings.xml"].begin(), files["PublishSettings.xml"].end());
    CHECK(settings.find("<ActionScriptVersion>3</ActionScriptVersion>") != std::string::npos);
    CHECK(settings.find("<DocumentClass>movie_fla.MainTimeline</DocumentClass>") != std::string::npos);

    // Played: the main timeline is an instance of its compiled class, its
    // frame scripts those addFrameScript gave, Star a symbol's class.
    Movie movie;
    fla::Symbol* star = movie.symbol("Star", fla::SymbolType::MovieClip, 3);
    star->extraAttributes.emplace_back("linkageClassName", "game.Star");
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 1);
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 2), *hat, "hat");
    movie.frame(*movie.actions, 0, 2);
    Played played(movie);
    played.runtime.setLibrary({star, hat}, {}, {});
    played.runtime.setClasses({}, "movie_fla.MainTimeline");
    played.runtime.setCompiledScripts({files["bin/abc/1.abc"]});
    played.runtime.start();
    played.runtime.step();
    played.runtime.step();
    played.showErrors();
    CHECK(played.errors.empty());
    const std::string expected = "frame 1 true true 3 true\nHi there\nframe 2 1\n";
    CHECK(played.output() == expected);
    if (played.output() != expected)
        std::printf("%s", played.output().c_str());
}
