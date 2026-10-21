// ActionScript 2's text fields: an object for each of the display list's
// text fields, placed by a timeline or made by createTextField, answering
// for where it is (_x, _alpha...); TextField.prototype's accessors and
// methods (resources/as2/text_field.as) ask of the field here.

#include "avm1_player.h"
#include "movie_runtime.h"
#include "../data/static_text.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"
#include "display/backend.h"
#include "display/display_object.h"
#include "display/text_field.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace fla {
namespace play {

namespace {

/// What a script sees a text field as: the field (while it lives), and
/// what only ActionScript 2 keeps of it.
class TextObject : public as3::HostObject
{
public:
    TextObject(MovieRuntime& runtime, const std::shared_ptr<display::TextField>& field)
        : runtime(runtime)
        , field(field)
    {}

    MovieRuntime& runtime;
    std::weak_ptr<display::TextField> field;
    /// html: whether htmlText is read as HTML.
    bool html = false;
    /// variable: the variable it shows, a name or a path to one.
    std::optional<std::string> variable;
    std::string antiAliasType = "normal";
    std::string gridFitType = "pixel";
    double thickness = 0.0;
    double sharpness = 0.0;
    std::optional<int32_t> tabIndex;
    /// What it showed as it was placed or made: what a variable it is
    /// given takes when the variable has no value.
    std::string initialText;
    as3::Val styleSheet;
    as3::Val filters;

    // Where it is and how it shows, as a clip's are; the rest is
    // TextField.prototype's.
    as3::ValuePtr get(const std::string& name) override
    {
        const std::shared_ptr<display::TextField> shown = field.lock();
        if (!shown)
            return nullptr;
        if (name == "_x")
            return as3::Val::number(shown->x());
        if (name == "_y")
            return as3::Val::number(shown->y());
        if (name == "_xscale")
            return as3::Val::number(shown->scaleX() * 100.0);
        if (name == "_yscale")
            return as3::Val::number(shown->scaleY() * 100.0);
        if (name == "_rotation")
            return as3::Val::number(shown->rotation());
        if (name == "_alpha")
            return as3::Val::number(shown->alpha() * 100.0);
        if (name == "_visible")
            return as3::Val::boolean(shown->visible());
        if (name == "_width")
            return as3::Val::number(shown->width());
        if (name == "_height")
            return as3::Val::number(shown->height());
        if (name == "_name")
            return as3::Val::string(shown->name());
        if (name == "_parent")
        {
            Clip* owner = shown->parent() ? shown->parent()->clip : nullptr;
            return owner ? as3::Val(runtime.objectFor(*owner)) : as3::Val();
        }
        if (name == "_highquality")
            return as3::Val::number(1);
        if (name == "_quality")
            return as3::Val::string("HIGH");
        return nullptr;
    }
    bool set(const std::string& name, as3::ValuePtr value) override
    {
        const std::shared_ptr<display::TextField> shown = field.lock();
        if (!shown)
            return false;
        const as3::Val given(value);
        const double number = given.isNumber() ? given.num : (value ? value->toNumber() : 0.0);
        if (name == "_x")
        {
            if (std::isnan(number))
                shown->clearScriptedX();
            else
                shown->setX(number);
        }
        else if (name == "_y")
        {
            if (std::isnan(number))
                shown->clearScriptedY();
            else
                shown->setY(number);
        }
        else if (name == "_xscale")
            shown->setScaleX(number / 100.0);
        else if (name == "_yscale")
            shown->setScaleY(number / 100.0);
        else if (name == "_rotation")
            shown->setRotation(number);
        else if (name == "_alpha")
            shown->setAlpha(number / 100.0);
        else if (name == "_visible")
            shown->setVisible(value && value->toBoolean());
        else if (name == "_width")
            shown->setWidth(number);
        else if (name == "_height")
            shown->setHeight(number);
        else if (name == "_name")
            shown->setName(value ? value->toString() : std::string());
        else
            return false;
        display::markDisplayChanged();
        if (runtime.changed)
            runtime.changed();
        return true;
    }
};

TextObject* textObjectOf(const as3::Val& value)
{
    return value.isObject() ? dynamic_cast<TextObject*>(value->host.get()) : nullptr;
}

/// The clip a field's variable is on, and its name there: "score" on the
/// field's own clip, "_root.score" or "/hat:score" on another.
Clip* variableOwner(MovieRuntime& runtime, const display::TextField& field, const std::string& variable,
    std::string& name)
{
    Clip* owner = field.parent() ? field.parent()->clip : nullptr;
    if (!owner)
        return nullptr;
    const size_t split = variable.find_last_of(".:");
    if (split == std::string::npos)
    {
        name = variable;
        return owner;
    }
    name = variable.substr(split + 1);
    return runtime.resolve(*owner, variable.substr(0, split));
}

/// The field's text into its variable, as typing or setting text puts it.
void storeVariable(MovieRuntime& runtime, as3::avm1::Interpreter& interpreter, const TextObject& object,
    const display::TextField& field)
{
    if (!object.variable)
        return;
    std::string name;
    if (Clip* owner = variableOwner(runtime, field, *object.variable, name))
        interpreter.setMember(runtime.objectFor(*owner), name, as3::Val::string(field.text()));
}

/// The variable's value shown in the field; one not set takes the field's
/// text when \a binding (the variable just given).
void showVariable(MovieRuntime& runtime, as3::avm1::Interpreter& interpreter, const TextObject& object,
    display::TextField& field, bool binding)
{
    if (!object.variable)
        return;
    std::string name;
    Clip* owner = variableOwner(runtime, field, *object.variable, name);
    if (!owner)
        return;
    const as3::Val value = interpreter.getMember(runtime.objectFor(*owner), name);
    if (value.isUndefined())
    {
        // Given a variable with no value, it shows what it first did, and
        // the variable takes that (when there is any).
        if (binding)
        {
            if (field.text() != object.initialText)
                field.setText(object.initialText);
            if (!object.initialText.empty())
                interpreter.setMember(runtime.objectFor(*owner), name, as3::Val::string(object.initialText));
        }
        return;
    }
    const std::string text = interpreter.toString(value);
    if (object.html ? field.htmlText() != text : field.text() != text)
    {
        if (object.html)
            field.setHtmlText(text);
        else
            field.setText(text);
        display::markDisplayChanged();
    }
}

bool sameName(const std::string& a, const std::string& b, bool anyCase)
{
    return a.size() == b.size() &&
        (!anyCase ? a == b : std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
        }));
}

} // namespace

std::shared_ptr<display::TextField> textFieldOfObject(const as3::Val& value)
{
    const TextObject* object = textObjectOf(value);
    return object ? object->field.lock() : nullptr;
}

as3::Val ActionsPlayer::textObject(const std::shared_ptr<display::TextField>& field)
{
    if (!field)
        return as3::Val();
    // One object for each field while it lives: what scripts set on it
    // stays.
    as3::Val found;
    _textObjects.erase(std::remove_if(_textObjects.begin(), _textObjects.end(),
                           [&](const auto& entry) {
                               const std::shared_ptr<display::DisplayObject> held = entry.first.lock();
                               if (held.get() == field.get())
                                   found = entry.second;
                               return !held;
                           }),
        _textObjects.end());
    if (found.isObject())
        return found;
    auto host = std::make_shared<TextObject>(_runtime, field);
    // A field from a timeline shows the variable the file gives it.
    if (field->timeline && field->timeline->element)
        if (const auto* element = dynamic_cast<const StaticText*>(field->timeline->element))
            for (const auto& [key, value] : element->extraAttributes)
            {
                if (key == "variableName" && !value.empty())
                    host->variable = value;
                if (key == "renderAsHTML")
                    host->html = value == "true";
            }
    host->initialText = field->text();
    field->swfVersion = _interpreter->version();
    found = as3::Value::makeHostObject(host);
    _textObjects.emplace_back(field, found);
    return found;
}

void ActionsPlayer::fieldPlaced(const std::shared_ptr<display::TextField>& field)
{
    // One showing a variable takes its value, or gives it its text.
    const as3::Val object = textObject(field);
    if (TextObject* text = textObjectOf(object); text && text->variable)
        showVariable(_runtime, *_interpreter, *text, *field, true);
}

as3::Val ActionsPlayer::textFieldNamed(Clip& clip, const std::string& name)
{
    if (!clip.display || !clip.display->isContainer() || name.empty())
        return as3::Val();
    const bool anyCase = _interpreter->version() < 7;
    for (const auto& child : static_cast<display::DisplayObjectContainer*>(clip.display.get())->children())
        if (child && child->kind() == display::ObjectKind::TextField && sameName(child->name(), name, anyCase))
            return textObject(std::static_pointer_cast<display::TextField>(child));
    return as3::Val();
}

as3::Val ActionsPlayer::textNatives()
{
    as3::VM& vm = _vm;
    as3::avm1::Interpreter& interpreter = *_interpreter;
    const as3::Val natives = vm.newObject();
    const auto define = [&](const std::string& name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    const auto changed = [this](display::TextField&) {
        display::markDisplayChanged();
        if (_runtime.changed)
            _runtime.changed();
    };
    // A property of the field: \a read gives it, \a write sets it (as
    // "set" + its name).
    using Field = std::shared_ptr<display::TextField>;
    const auto property = [&](const std::string& name, std::function<as3::Val(TextObject&, display::TextField&)> read,
                              std::function<void(TextObject&, display::TextField&, const as3::Val&)> write) {
        define(name, [read](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
            TextObject* object = textObjectOf(self);
            const Field field = object ? object->field.lock() : nullptr;
            return field ? read(*object, *field) : as3::Val();
        });
        if (write)
            define("set" + name, [write, changed](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
                TextObject* object = textObjectOf(self);
                const Field field = object ? object->field.lock() : nullptr;
                if (field)
                {
                    write(*object, *field, args.empty() ? as3::Val() : args[0]);
                    changed(*field);
                }
                return as3::Val();
            });
    };
    const auto boolean = [&interpreter](const as3::Val& value) { return interpreter.toBoolean(value); };
    const auto rgb = [&interpreter](const as3::Val& value) {
        return as3::toUint32(interpreter.toNumber(value)) & 0xffffff;
    };
    const auto text = [&interpreter](const as3::Val& value) { return interpreter.toString(value); };
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    };

    // Read, it shows its variable as the variable is now.
    property("text",
        [this, &interpreter](TextObject& o, display::TextField& f) {
            showVariable(_runtime, interpreter, o, f, false);
            return as3::Val::string(f.text());
        },
        [this, &interpreter, text](TextObject& o, display::TextField& f, const as3::Val& v) {
            f.setText(text(v));
            storeVariable(_runtime, interpreter, o, f);
        });
    property("htmlText",
        [this, &interpreter](TextObject& o, display::TextField& f) {
            showVariable(_runtime, interpreter, o, f, false);
            // Empty, it reads empty before SWF 8.
            return as3::Val::string(!o.html ? f.text()
                    : f.length() == 0 && f.swfVersion < 8 ? std::string()
                                                         : f.htmlText());
        },
        [text](TextObject& o, display::TextField& f, const as3::Val& v) {
            if (o.html)
                f.setHtmlText(text(v));
            else
                f.setText(text(v));
        });
    property("html", [](TextObject& o, display::TextField&) { return as3::Val::boolean(o.html); },
        [boolean](TextObject& o, display::TextField&, const as3::Val& v) { o.html = boolean(v); });
    property("textColor", [](TextObject&, display::TextField& f) { return as3::Val::number(f.textColor() & 0xffffff); },
        [rgb](TextObject&, display::TextField& f, const as3::Val& v) {
            // Every character's, and what is typed next.
            const uint32_t color = rgb(v);
            display::TextFormat format;
            format.color = color;
            f.setTextFormat(format);
            display::TextFormat next = f.defaultTextFormat();
            next.color = color;
            f.setDefaultTextFormat(next);
        });
    property("background", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.background()); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.setBackground(boolean(v)); });
    property("backgroundColor",
        [](TextObject&, display::TextField& f) { return as3::Val::number(f.backgroundColor() & 0xffffff); },
        [rgb](TextObject&, display::TextField& f, const as3::Val& v) { f.setBackgroundColor(rgb(v)); });
    property("border", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.border()); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.setBorder(boolean(v)); });
    property("borderColor", [](TextObject&, display::TextField& f) { return as3::Val::number(f.borderColor() & 0xffffff); },
        [rgb](TextObject&, display::TextField& f, const as3::Val& v) { f.setBorderColor(rgb(v)); });
    property("embedFonts", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.embedFonts); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.embedFonts = boolean(v); });
    property("length", [](TextObject&, display::TextField& f) { return as3::Val::number(f.length()); }, nullptr);
    property("textWidth", [](TextObject&, display::TextField& f) { return as3::Val::number(std::trunc(f.textWidth())); },
        nullptr);
    property("textHeight",
        [](TextObject&, display::TextField& f) { return as3::Val::number(std::trunc(f.textHeight())); }, nullptr);
    property("mouseWheelEnabled", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.mouseWheelEnabled); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.mouseWheelEnabled = boolean(v); });
    property("multiline", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.multiline()); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.setMultiline(boolean(v)); });
    property("selectable", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.selectable); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.selectable = boolean(v); });
    property("wordWrap", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.wordWrap()); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.setWordWrap(boolean(v)); });
    property("password", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.displayAsPassword()); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.setDisplayAsPassword(boolean(v)); });
    property("condenseWhite", [](TextObject&, display::TextField& f) { return as3::Val::boolean(f.condenseWhite); },
        [boolean](TextObject&, display::TextField& f, const as3::Val& v) { f.condenseWhite = boolean(v); });
    // An unset variable reads null.
    property("variable",
        [](TextObject& o, display::TextField&) { return o.variable ? as3::Val::string(*o.variable) : as3::Val::null(); },
        [this, &interpreter, text](TextObject& o, display::TextField& f, const as3::Val& v) {
            if (v.isNullish())
                o.variable.reset();
            else
                o.variable = text(v);
            showVariable(_runtime, interpreter, o, f, true);
        });
    property("autoSize",
        [](TextObject&, display::TextField& f) {
            static const char* const names[] = {"none", "left", "center", "right"};
            return as3::Val::string(names[static_cast<int>(f.autoSize())]);
        },
        [text, lower](TextObject&, display::TextField& f, const as3::Val& v) {
            display::TextField::AutoSize size = display::TextField::AutoSize::None;
            if (v.isBoolean())
                size = v.toBoolean() ? display::TextField::AutoSize::Left : display::TextField::AutoSize::None;
            else
            {
                const std::string mode = lower(text(v));
                size = mode == "left"   ? display::TextField::AutoSize::Left
                    : mode == "center" ? display::TextField::AutoSize::Center
                    : mode == "right"  ? display::TextField::AutoSize::Right
                                       : display::TextField::AutoSize::None;
            }
            f.setAutoSize(size);
        });
    property("type",
        [](TextObject&, display::TextField& f) {
            return as3::Val::string(f.type() == display::TextField::Type::Input ? "input" : "dynamic");
        },
        [text, lower](TextObject&, display::TextField& f, const as3::Val& v) {
            const std::string type = lower(text(v));
            if (type == "input")
                f.setType(display::TextField::Type::Input);
            else if (type == "dynamic")
                f.setType(display::TextField::Type::Dynamic);
        });
    property("hscroll", [](TextObject&, display::TextField& f) { return as3::Val::number(f.scrollH()); },
        [&interpreter](TextObject&, display::TextField& f, const as3::Val& v) {
            const double pixels = as3::toInt32(interpreter.toNumber(v));
            f.setScrollH(std::clamp(pixels, 0.0, f.maxScrollH()));
        });
    property("maxhscroll", [](TextObject&, display::TextField& f) { return as3::Val::number(f.maxScrollH()); }, nullptr);
    property("scroll", [](TextObject&, display::TextField& f) { return as3::Val::number(f.scrollV()); },
        [&interpreter](TextObject&, display::TextField& f, const as3::Val& v) {
            const double line = interpreter.toNumber(v);
            if (!std::isnan(line))
                f.setScrollV(static_cast<int>(std::clamp(line, 1.0, double(std::max(1, f.maxScrollV())))));
        });
    property("maxscroll", [](TextObject&, display::TextField& f) { return as3::Val::number(f.maxScrollV()); }, nullptr);
    property("bottomScroll", [](TextObject&, display::TextField& f) { return as3::Val::number(f.bottomScrollV()); },
        nullptr);
    // No limit reads null.
    property("maxChars",
        [](TextObject&, display::TextField& f) {
            return f.maxChars != 0 ? as3::Val::number(f.maxChars) : as3::Val::null();
        },
        [&interpreter](TextObject&, display::TextField& f, const as3::Val& v) {
            f.maxChars = as3::toInt32(interpreter.toNumber(v));
        });
    // None reads null, and an empty one is none.
    property("restrict",
        [](TextObject&, display::TextField& f) {
            return f.restrictTo.empty() ? as3::Val::null() : as3::Val::string(f.restrictTo);
        },
        [text](TextObject&, display::TextField& f, const as3::Val& v) {
            f.restrictTo = v.isNullish() ? std::string() : text(v);
        });
    property("antiAliasType", [](TextObject& o, display::TextField&) { return as3::Val::string(o.antiAliasType); },
        [text](TextObject& o, display::TextField&, const as3::Val& v) {
            const std::string type = text(v);
            if (type == "advanced" || type == "normal")
                o.antiAliasType = type;
        });
    property("gridFitType", [](TextObject& o, display::TextField&) { return as3::Val::string(o.gridFitType); },
        [text](TextObject& o, display::TextField&, const as3::Val& v) {
            const std::string type = text(v);
            if (type == "none" || type == "pixel" || type == "subpixel")
                o.gridFitType = type;
        });
    property("thickness", [](TextObject& o, display::TextField&) { return as3::Val::number(o.thickness); },
        [&interpreter](TextObject& o, display::TextField&, const as3::Val& v) {
            o.thickness = static_cast<float>(interpreter.toNumber(v));
        });
    property("sharpness", [](TextObject& o, display::TextField&) { return as3::Val::number(o.sharpness); },
        [&interpreter](TextObject& o, display::TextField&, const as3::Val& v) {
            o.sharpness = static_cast<float>(interpreter.toNumber(v));
        });
    property("styleSheet", [](TextObject& o, display::TextField&) { return o.styleSheet; },
        [](TextObject& o, display::TextField&, const as3::Val& v) { o.styleSheet = v.isObject() ? v : as3::Val(); });
    property("tabIndex",
        [](TextObject& o, display::TextField&) {
            return o.tabIndex ? as3::Val::number(static_cast<uint32_t>(*o.tabIndex)) : as3::Val();
        },
        [&interpreter](TextObject& o, display::TextField&, const as3::Val& v) {
            if (v.isNullish())
                o.tabIndex.reset();
            else
                o.tabIndex = static_cast<int32_t>(as3::toUint32(interpreter.toNumber(v)));
        });

    // Formats, as plain objects of what is set: the library's TextFormat
    // keeps one out of sight.
    const auto formatObject = [&vm, &interpreter](const display::TextFormat& f) {
        const as3::Val out = vm.newObject();
        const auto put = [&](const char* name, const as3::Val& value) { interpreter.setMember(out, name, value); };
        if (f.font)
            put("font", as3::Val::string(*f.font));
        if (f.size)
            put("size", as3::Val::number(*f.size));
        if (f.color)
            put("color", as3::Val::number(*f.color & 0xffffff));
        if (f.bold)
            put("bold", as3::Val::boolean(*f.bold));
        if (f.italic)
            put("italic", as3::Val::boolean(*f.italic));
        if (f.underline)
            put("underline", as3::Val::boolean(*f.underline));
        if (f.url)
            put("url", as3::Val::string(*f.url));
        if (f.target)
            put("target", as3::Val::string(*f.target));
        if (f.align)
            put("align", as3::Val::string(*f.align));
        if (f.leftMargin)
            put("leftMargin", as3::Val::number(*f.leftMargin));
        if (f.rightMargin)
            put("rightMargin", as3::Val::number(*f.rightMargin));
        if (f.indent)
            put("indent", as3::Val::number(*f.indent));
        if (f.leading)
            put("leading", as3::Val::number(*f.leading));
        if (f.blockIndent)
            put("blockIndent", as3::Val::number(*f.blockIndent));
        if (f.bullet)
            put("bullet", as3::Val::boolean(*f.bullet));
        if (f.kerning)
            put("kerning", as3::Val::boolean(*f.kerning));
        if (f.letterSpacing)
            put("letterSpacing", as3::Val::number(*f.letterSpacing));
        put("display", as3::Val::string("block"));
        if (f.font)
            put("tabStops", vm.newArray({}));
        return out;
    };
    const auto formatOf = [&interpreter](const as3::Val& object) {
        display::TextFormat f;
        if (!object.isObject())
            return f;
        const auto value = [&](const char* name) -> std::optional<as3::Val> {
            const as3::Val v = interpreter.getMember(object, name);
            return v.isNullish() ? std::nullopt : std::optional<as3::Val>(v);
        };
        if (auto v = value("font"))
            f.font = interpreter.toString(*v);
        if (auto v = value("size"))
            f.size = interpreter.toNumber(*v);
        if (auto v = value("color"))
            f.color = as3::toUint32(interpreter.toNumber(*v)) & 0xffffff;
        if (auto v = value("bold"))
            f.bold = interpreter.toBoolean(*v);
        if (auto v = value("italic"))
            f.italic = interpreter.toBoolean(*v);
        if (auto v = value("underline"))
            f.underline = interpreter.toBoolean(*v);
        if (auto v = value("url"))
            f.url = interpreter.toString(*v);
        if (auto v = value("target"))
            f.target = interpreter.toString(*v);
        if (auto v = value("align"))
            f.align = interpreter.toString(*v);
        if (auto v = value("leftMargin"))
            f.leftMargin = interpreter.toNumber(*v);
        if (auto v = value("rightMargin"))
            f.rightMargin = interpreter.toNumber(*v);
        if (auto v = value("indent"))
            f.indent = interpreter.toNumber(*v);
        if (auto v = value("leading"))
            f.leading = interpreter.toNumber(*v);
        if (auto v = value("blockIndent"))
            f.blockIndent = interpreter.toNumber(*v);
        if (auto v = value("bullet"))
            f.bullet = interpreter.toBoolean(*v);
        if (auto v = value("kerning"))
            f.kerning = interpreter.toBoolean(*v);
        if (auto v = value("letterSpacing"))
            f.letterSpacing = interpreter.toNumber(*v);
        return f;
    };
    const auto fieldOf = [](const as3::Val& self) -> Field {
        TextObject* object = textObjectOf(self);
        return object ? object->field.lock() : nullptr;
    };
    // getTextFormat(begin, end), (index) or (): an empty range has no
    // format.
    define("getTextFormat", [&interpreter, fieldOf, formatObject](as3::VM& vm, const as3::Val& self,
                                as3::Args args) -> as3::Val {
        const Field field = fieldOf(self);
        if (!field)
            return as3::Val();
        int64_t begin = 0, end = std::numeric_limits<int32_t>::max();
        if (args.size() >= 2)
        {
            begin = as3::toInt32(interpreter.toNumber(args[0]));
            end = as3::toInt32(interpreter.toNumber(args[1]));
        }
        else if (args.size() == 1)
        {
            begin = as3::toInt32(interpreter.toNumber(args[0]));
            end = begin + 1;
        }
        const auto clamp = [&](int64_t i) { return static_cast<int>(std::min<int64_t>(std::max<int64_t>(i, 0), field->length())); };
        const int from = clamp(begin), to = clamp(end);
        if (from >= to)
            return vm.newObject();
        return formatObject(field->getTextFormat(from, to));
    });
    // setTextFormat(begin, end, fields), (index, fields) or (fields).
    define("setTextFormat", [&interpreter, fieldOf, formatOf, changed](as3::VM&, const as3::Val& self,
                                as3::Args args) -> as3::Val {
        const Field field = fieldOf(self);
        if (!field || args.empty())
            return as3::Val();
        int begin = 0, end = field->length();
        as3::Val fields;
        if (args.size() >= 3)
        {
            begin = std::max(0, as3::toInt32(interpreter.toNumber(args[0])));
            end = std::max(0, as3::toInt32(interpreter.toNumber(args[1])));
            fields = args[2];
        }
        else if (args.size() == 2)
        {
            const int32_t at = as3::toInt32(interpreter.toNumber(args[0]));
            begin = std::max(0, at);
            end = std::max(0, static_cast<int>(int64_t(at) + 1));
            fields = args[1];
        }
        else
            fields = args[0];
        if (!fields.isObject())
            return as3::Val();
        begin = std::min(begin, field->length());
        end = std::min(end, field->length());
        if (begin < end)
            field->setTextFormat(formatOf(fields), begin, end);
        changed(*field);
        return as3::Val();
    });
    define("getNewTextFormat", [fieldOf, formatObject](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        const Field field = fieldOf(self);
        return field ? formatObject(field->defaultTextFormat()) : as3::Val();
    });
    define("setNewTextFormat", [fieldOf, formatOf](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        const Field field = fieldOf(self);
        if (field && !args.empty() && args[0].isObject())
        {
            display::TextFormat format = field->defaultTextFormat();
            format.merge(formatOf(args[0]));
            field->setDefaultTextFormat(format);
        }
        return as3::Val();
    });
    define("replaceSel", [this, &interpreter, fieldOf, changed](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        const Field field = fieldOf(self);
        if (!field)
            return as3::Val();
        field->replaceSelectedText(args.empty() ? std::string("undefined") : interpreter.toString(args[0]));
        if (TextObject* object = textObjectOf(self))
            storeVariable(_runtime, interpreter, *object, *field);
        changed(*field);
        return as3::Val();
    });
    define("replaceText", [&interpreter, fieldOf, changed](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        const Field field = fieldOf(self);
        if (!field || args.size() < 3)
            return as3::Val();
        const auto index = [&](size_t i) {
            const double n = interpreter.toNumber(args[i]);
            return std::isnan(n) || n < 0 ? 0 : static_cast<int>(std::min<double>(n, field->length()));
        };
        field->replaceText(index(0), std::max(index(0), index(1)), interpreter.toString(args[2]));
        changed(*field);
        return as3::Val();
    });
    define("getDepth", [fieldOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        const Field field = fieldOf(self);
        const std::optional<int> depth = field ? field->depthNumber() : std::nullopt;
        return depth ? as3::Val::number(*depth) : as3::Val();
    });
    // removeTextField: only one at a depth scripts use.
    define("removeTextField", [this, fieldOf](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        const Field field = fieldOf(self);
        const std::optional<int> depth = field ? field->depthNumber() : std::nullopt;
        if (field && field->parent() && depth && *depth >= 0 && *depth < 2130706416 - 16384)
        {
            field->parent()->removeChild(field.get());
            display::markDisplayChanged();
            if (_runtime.changed)
                _runtime.changed();
        }
        return as3::Val();
    });
    define("getFontList", [&vm](as3::VM&, const as3::Val&, as3::Args) -> as3::Val {
        std::vector<as3::Val> names;
        for (const char* name : {"_sans", "_serif", "_typewriter", "Arial", "Courier New", "Times New Roman"})
            names.push_back(as3::Val::string(name));
        return vm.newArray(std::move(names));
    });
    return natives;
}

void ActionsPlayer::updateTextBindings()
{
    // What each field's variable holds now, shown.
    for (const auto& [held, value] : _textObjects)
    {
        TextObject* object = textObjectOf(value);
        const std::shared_ptr<display::DisplayObject> shown = held.lock();
        if (object && object->variable && shown)
            showVariable(_runtime, *_interpreter, *object, static_cast<display::TextField&>(*shown), false);
    }
}

} // namespace play
} // namespace fla
