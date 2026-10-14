// XML (XUL) dialogs: reading the XML, the values controls start with, and
// scripts showing them, headless and through a stand-in for the person.
#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/new_document.h"
#include "../src/edit/selection.h"
#include "../src/jsfl/engine.h"
#include "../src/jsfl/flfile.h"
#include "../src/jsfl/xul.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

namespace {

using jsfl::xul::Dialog;
using jsfl::xul::Kind;

/// One new document; dialogs answered by \a person when set, otherwise
/// shown to no one.
class XulHost : public jsfl::Host
{
public:
    XulHost() : document(fla::createEmptyDocument(550, 400, 24.0)) { edited = document->document->currentScene(); }

    std::unique_ptr<fla::FLADocument> document;
    fla::CommandStack stack;
    fla::Selection chosen;
    fla::Timeline* edited = nullptr;
    std::string output;
    std::function<void(Dialog&)> person;
    int shown = 0;

    bool showDialog(Dialog& dialog) override
    {
        if (!person)
            return false;
        ++shown;
        person(dialog);
        return true;
    }

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
    std::vector<fla::FLADocument*> documents() override { return {document.get()}; }
    fla::FLADocument* activeDocument() override { return document.get(); }
    void activate(fla::FLADocument*) override {}
    fla::FLADocument* open(const std::string&, std::string& error) override
    {
        error = "not in these tests";
        return nullptr;
    }
    fla::FLADocument* create(std::string& error) override
    {
        error = "not in these tests";
        return nullptr;
    }
    bool save(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "not in these tests";
        return false;
    }
    bool close(fla::FLADocument*, bool) override { return false; }
    std::string path(fla::FLADocument*) override { return {}; }
    std::string name(fla::FLADocument*) override { return "Untitled"; }
    bool modified(fla::FLADocument*) override { return false; }
    fla::CommandStack& history(fla::FLADocument*) override { return stack; }
    fla::Selection& selection(fla::FLADocument*) override { return chosen; }
    fla::Timeline* editedTimeline(fla::FLADocument*) override { return edited; }
    void editTimeline(fla::FLADocument*, fla::Timeline* timeline) override { edited = timeline; }
    int currentFrame(fla::FLADocument*) override { return 0; }
    void setCurrentFrame(fla::FLADocument*, int) override {}
    int currentLayer(fla::FLADocument*) override { return 0; }
    void setCurrentLayer(fla::FLADocument*, int) override {}
    jsfl::Rect bounds(fla::FLADocument*, const fla::Element*) override { return {}; }
    bool exportPNG(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "not in these tests";
        return false;
    }
};

struct Fixture
{
    XulHost host;
    jsfl::Engine engine{host};
    bool run(const std::string& source)
    {
        jsfl::RunOptions options;
        options.timeLimit = 20.0;
        const jsfl::RunResult result = engine.runString(source, "xul.jsfl", options);
        if (!result.ok)
            std::printf("    script error: %s\n", result.report().c_str());
        return result.ok;
    }
};

/// A field of a dialog's result, or "<none>".
std::string field(const jsfl::Value& result, const char* name)
{
    const jsfl::Value* value = result.field(name);
    return value ? value->toString() : std::string("<none>");
}

Dialog loaded(const std::string& xml)
{
    Dialog dialog;
    std::string error;
    if (!dialog.load(xml, error))
        std::printf("    load error: %s\n", error.c_str());
    return dialog;
}

} // namespace

TEST(xul_reads_a_dialog_as_scripts_write_it)
{
    // One line, escaped quotes, a label pointing at its text box.
    Dialog dialog;
    std::string error;
    CHECK(dialog.load("<dialog title=\"The Shake Maker\" buttons=\"accept, cancel\"><label value=\"Intensity:\" "
                      "control=\"panel_int\"/><textbox id=\"panel_int\" size=\"24\" value=\"20\" /><spacer /><separator />"
                      "<checkbox id=\"panel_taperOff\" label=\"Taper Off?\" checked=\"true\"/></dialog>",
        error));
    CHECK(dialog.title == "The Shake Maker");
    CHECK(dialog.buttons.size() == 2 && dialog.buttons[0] == "accept" && dialog.buttons[1] == "cancel");
    CHECK(dialog.controls().size() == 2);
    CHECK(dialog.get("panel_int") == "20");
    CHECK(dialog.get("panel_taperOff") == "true");
    dialog.close("accept");
    const jsfl::Value result = dialog.result();
    CHECK(field(result, "dismiss") == "accept");
    CHECK(field(result, "panel_int") == "20");
    CHECK(field(result, "panel_taperOff") == "true");
    CHECK(result.fields.size() == 3);
}

TEST(xul_parsing_forgives_what_scripts_leave_in)
{
    // A declaration and comment first, single quotes, an unescaped
    // ampersand, a script with < and && in it, a stray close tag.
    Dialog dialog;
    std::string error;
    CHECK(dialog.load("<?xml version=\"1.0\"?>\n<!-- options -->\n<dialog title='Tom & Jerry' buttons='accept'>"
                      "<vbox><label value='a &lt; b &amp; c'/></hbox><textbox id='t' value='x&#65;'/></vbox>"
                      "<script>function f(a, b) { if (a < b && b > 0) return 1; }</script>"
                      "<checkbox id='c' label='C'/></dialog>",
        error));
    CHECK(dialog.title == "Tom & Jerry");
    CHECK(dialog.buttons.size() == 1);
    CHECK(dialog.get("t") == "xA");
    CHECK(dialog.get("c") == "false");
    const jsfl::xul::Node& vbox = dialog.root().children[0];
    CHECK(vbox.children[0].attr("value") == "a < b & c");
    // The script block's code survives whole.
    bool found = false;
    for (const jsfl::xul::Node& child : dialog.root().children)
    {
        if (child.is("script"))
            found = child.text.find("a < b && b > 0") != std::string::npos;
    }
    CHECK(found);

    std::string none;
    Dialog empty;
    CHECK(!empty.load("just text", none));
    CHECK(!none.empty());
    Dialog noDialog;
    CHECK(!noDialog.load("<vbox><textbox id='a'/></vbox>", none));
}

TEST(xul_controls_start_with_their_defaults)
{
    // The shapes real scripts build: radio groups, menus (menupop and
    // menupopup), a list box, a grid in a content wrapper, a color chip, a
    // slider, hidden properties, buttons.
    Dialog dialog = loaded(
        "<dialog title='Export' buttons='accept,cancel'>"
        "<content><grid><columns><column flex='1'/><column flex='2'/></columns><rows>"
        "<row><label value='Name'/><textbox id='name' value='hero'/></row>"
        "<row><label value='Format'/><menulist id='format'><menupop>"
        "<menuitem label='PNG' value='png'/><menuitem label='JPEG' value='jpg' selected='true'/></menupop></menulist></row>"
        "</rows></grid></content>"
        "<radiogroup id='side'><radio label='Left' value='L'/><radio label='Right' value='R' selected='true'/></radiogroup>"
        "<radiogroup id='first'><radio label='One'/><radio label='Two'/></radiogroup>"
        "<menulist id='plain'><menupopup><menuitem label='Alpha'/><menuitem label='Beta'/></menupopup></menulist>"
        "<menulist id='typed' editable='true' value='custom'><menupop><menuitem label='12' value='12'/></menupop></menulist>"
        "<listbox id='list' rows='3'><listitem label='a' value='1'/><listitem label='b' value='2'/></listbox>"
        "<listbox id='picked'><listitem label='a' value='1'/><listitem label='b' value='2' selected='true'/></listbox>"
        "<colorchip id='tint' color='#ff6600'/><colorchip id='number' color='#0000FF' format='integer'/>"
        "<popupslider id='alpha' value='75' minvalue='0' maxvalue='100'/>"
        "<property id='hidden' value='secret'/>"
        "<choosefile id='file'/><targetlist id='target'/>"
        "<checkbox id='plainBox' label='x'/><checkbox id='valued' label='y' value='true'/>"
        "<hbox><button id='go' label='Go'/><label id='status' value='ready'/></hbox>"
        "</dialog>");
    CHECK(dialog.get("name") == "hero");
    CHECK(dialog.get("format") == "jpg");
    CHECK(dialog.get("side") == "R");
    CHECK(dialog.get("first") == "One");
    CHECK(dialog.get("plain") == "Alpha");
    CHECK(dialog.get("typed") == "custom");
    CHECK(dialog.get("list").empty());
    CHECK(dialog.get("picked") == "2");
    CHECK(dialog.get("tint") == "#FF6600");
    CHECK(dialog.get("number") == "255");
    CHECK(dialog.get("alpha") == "75");
    CHECK(dialog.get("hidden") == "secret");
    CHECK(dialog.get("file").empty());
    CHECK(dialog.get("target").empty());
    CHECK(dialog.get("plainBox") == "false");
    CHECK(dialog.get("valued") == "true");
    CHECK(dialog.get("status") == "ready");
    dialog.close("cancel");
    const jsfl::Value result = dialog.result();
    CHECK(field(result, "dismiss") == "cancel");
    CHECK(field(result, "hidden") == "secret");
    // Buttons and labels have no value to give back.
    CHECK(field(result, "go") == "<none>");
    CHECK(field(result, "status") == "<none>");
    CHECK(dialog.find("format")->items.size() == 2);
}

TEST(xul_set_follows_each_kind_of_control)
{
    Dialog dialog = loaded("<dialog><checkbox id='c'/><menulist id='m'><menupop><menuitem label='A' value='a'/>"
                           "<menuitem label='B' value='b'/></menupop></menulist><radiogroup id='r'><radio label='X'/>"
                           "<radio label='Y'/></radiogroup><colorchip id='k'/><textbox id='t'/></dialog>");
    int changes = 0;
    dialog.changed = [&](jsfl::xul::Control&) { ++changes; };
    dialog.set("c", jsfl::Value("true"));
    CHECK(dialog.get("c") == "true");
    dialog.set("c", jsfl::Value("false"));
    CHECK(dialog.get("c") == "false");
    dialog.set("c", jsfl::Value(true));
    CHECK(dialog.get("c") == "true");
    dialog.set("m", jsfl::Value("b"));
    CHECK(dialog.get("m") == "b");
    dialog.set("m", jsfl::Value("A"));
    CHECK(dialog.get("m") == "a");
    dialog.set("r", jsfl::Value("Y"));
    CHECK(dialog.get("r") == "Y");
    dialog.set("k", jsfl::Value(0x00FF00));
    CHECK(dialog.get("k") == "#00FF00");
    dialog.set("t", jsfl::Value(42));
    CHECK(dialog.get("t") == "42");
    CHECK(changes == 8);
    bool threw = false;
    try
    {
        dialog.set("missing", jsfl::Value(1));
    }
    catch (const jsfl::Error&)
    {
        threw = true;
    }
    CHECK(threw);
}

TEST(xul_headless_panel_is_accepted_with_defaults)
{
    Fixture f;
    CHECK(f.run("var p = fl.xmlPanelFromString('<dialog title=\"Shake\" buttons=\"accept, cancel\">"
                "<textbox id=\"amount\" value=\"20\"/><checkbox id=\"taper\" label=\"Taper\" checked=\"true\"/>"
                "<checkbox id=\"loop\" label=\"Loop\"/></dialog>');\n"
                "fl.trace([p.dismiss, p.amount, p.taper, p.loop, typeof p.taper].join(' '));"));
    CHECK(f.host.output.find("accept 20 true false string\n") != std::string::npos);
    // A note says the dialog was not shown.
    CHECK(f.host.output.find("Shake") != std::string::npos);
    CHECK(f.host.shown == 0);
}

TEST(xul_script_blocks_and_oncreate_set_the_starting_values)
{
    // As a script sizes its export fields from the document.
    Fixture f;
    CHECK(f.run("var x = '<dialog title=\"Export\" buttons=\"accept, cancel\">';\n"
                "x += '<textbox id=\"width\" value=\"\" oncreate=\"setW()\"/>';\n"
                "x += '<textbox id=\"height\" value=\"\" oncreate=\"setH()\"/>';\n"
                "x += '<script>var fdoc = fl.getDocumentDOM();';\n"
                "x += 'function setW() { fl.xmlui.set(\"width\", fdoc.width); }';\n"
                "x += 'function setH() { if (fl.xmlui.getEnabled(\"height\")) fl.xmlui.set(\"height\", fdoc.height / 2); }';\n"
                "x += '</script></dialog>';\n"
                "var r = fl.xmlPanelFromString(x);\n"
                "fl.trace(r.width + 'x' + r.height);"));
    CHECK(f.host.output.find("550x200\n") != std::string::npos);
}

TEST(xul_handlers_change_values_through_xmlui)
{
    // The person presses a button and ticks a box; handlers the script
    // defined update other controls, then one accepts the dialog.
    Fixture f;
    f.host.person = [](Dialog& dialog) {
        dialog.click(*dialog.find("double")->node);
        dialog.toggle(*dialog.find("lock"), true);
        dialog.edit(*dialog.find("name"), "Bob");
        dialog.choose(*dialog.find("mode"), 1);
        if (!dialog.closed())
            dialog.click(*dialog.find("done")->node);
    };
    CHECK(f.run("function twice() { fl.xmlui.set('size', Number(fl.xmlui.get('size')) * 2); }\n"
                "function locked() {\n"
                "  var on = fl.xmlui.get('lock') == 'true';\n"
                "  fl.xmlui.setEnabled('size', !on); fl.xmlui.setVisible('note', on);\n"
                "}\n"
                "var seen = [];\n"
                "var r = fl.xmlPanelFromString('<dialog buttons=\"accept, cancel\">' +\n"
                "  '<textbox id=\"size\" value=\"5\"/><button id=\"double\" label=\"x2\" oncommand=\"twice()\"/>' +\n"
                "  '<checkbox id=\"lock\" label=\"Lock\" oncommand=\"locked()\"/><label id=\"note\" value=\"locked\"/>' +\n"
                "  '<textbox id=\"name\" onchange=\"seen.push(fl.xmlui.get(\\'name\\'))\"/>' +\n"
                "  '<menulist id=\"mode\" oncommand=\"seen.push(fl.xmlui.get(\\'mode\\'))\"><menupop>' +\n"
                "  '<menuitem label=\"Fast\" value=\"f\"/><menuitem label=\"Slow\" value=\"s\"/></menupop></menulist>' +\n"
                "  '<button id=\"done\" label=\"Done\" oncommand=\"fl.xmlui.accept()\"/></dialog>');\n"
                "fl.trace([r.dismiss, r.size, r.lock, r.name, r.mode, seen.join('+')].join(' '));"));
    CHECK(f.host.shown == 1);
    CHECK(f.host.output == "accept 10 true Bob s Bob+s\n");
    if (f.host.output != "accept 10 true Bob s Bob+s\n")
        std::printf("    got: %s", f.host.output.c_str());
}

TEST(xul_closing_the_window_cancels)
{
    Fixture f;
    f.host.person = [](Dialog& dialog) { dialog.close("cancel"); };
    CHECK(f.run("var r = fl.xmlPanelFromString('<dialog><textbox id=\"a\" value=\"1\"/></dialog>');\n"
                "fl.trace(r.dismiss + ' ' + r.a);"));
    CHECK(f.host.output == "cancel 1\n");
    // Shown and left open is a cancel too.
    f.host.output.clear();
    f.host.person = [](Dialog&) {};
    CHECK(f.run("fl.trace(fl.xmlPanelFromString('<dialog/>').dismiss);"));
    CHECK(f.host.output == "cancel\n");
}

TEST(xul_handler_errors_are_traced_not_fatal)
{
    Fixture f;
    CHECK(f.run("var r = fl.xmlPanelFromString('<dialog><textbox id=\"a\" value=\"1\" oncreate=\"noSuch()\"/></dialog>');\n"
                "fl.trace('still ' + r.a);"));
    CHECK(f.host.output.find("noSuch") != std::string::npos);
    CHECK(f.host.output.find("still 1\n") != std::string::npos);
}

TEST(xul_item_lists_can_be_replaced)
{
    Fixture f;
    f.host.person = [](Dialog& dialog) { dialog.click(*dialog.find("fill")->node); dialog.close("accept"); };
    CHECK(f.run("function fill() {\n"
                "  fl.xmlui.setControlItemElements('pick', [{label: 'One', value: '1'}, {label: 'Two', value: '2'}]);\n"
                "  fl.xmlui.set('pick', '2');\n"
                "  var items = fl.xmlui.getControlItemElements('pick');\n"
                "  fl.trace(items.length + ' ' + items[1].label + ' ' + fl.xmlui.getControlItemElement('pick').value);\n"
                "}\n"
                "var r = fl.xmlPanelFromString('<dialog><menulist id=\"pick\"><menupop><menuitem label=\"none\"/></menupop>' +\n"
                "  '</menulist><button id=\"fill\" label=\"Fill\" oncommand=\"fill()\"/></dialog>');\n"
                "fl.trace(r.pick);"));
    CHECK(f.host.output == "2 Two 2\n2\n");
}

TEST(xul_panels_from_files_and_the_document)
{
    Fixture f;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_jsfl_tests" / "xul";
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "options.xml";
    {
        std::ofstream out(file, std::ios::binary);
        out << "\xEF\xBB\xBF<dialog id='opts' title='Options' buttons='accept, cancel'>\n"
               "  <radiogroup id='where'><radio label='Here' value='here' selected='true'/><radio label='There'/></radiogroup>\n"
               "</dialog>\n";
    }
    const std::string uri = jsfl::uri::fromPath(file.u8string());
    CHECK(f.run("var a = fl.xmlPanel('" + uri + "'); var b = fl.getDocumentDOM().xmlPanel('" + uri + "');\n"
                "fl.trace(a.where + ' ' + b.where + ' ' + b.dismiss);"));
    CHECK(f.host.output.find("here here accept\n") != std::string::npos);

    jsfl::RunOptions options;
    const jsfl::RunResult missing = f.engine.runString("fl.xmlPanel('file:///Z|/no/such/dialog.xml');", "missing.jsfl", options);
    CHECK(!missing.ok);
    CHECK(missing.message.find("could not read") != std::string::npos);
    const jsfl::RunResult outside = f.engine.runString("fl.xmlui.get('a');", "outside.jsfl", options);
    CHECK(!outside.ok);
    CHECK(outside.message.find("dialog is open") != std::string::npos);
    std::filesystem::remove_all(folder);
}
