// flash.text: TextField, TextFormat, Font and the constants classes.

#include "native.h"
#include "../css.h"

#include <algorithm>

namespace fla {
namespace play {
namespace script {

namespace {

display::TextField& field(VM& vm, const Val& self)
{
    display::DisplayObject* o = displayHost(vm, self).object();
    if (o->kind() != display::ObjectKind::TextField)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: TextField expected.", 1034);
    return static_cast<display::TextField&>(*o);
}

display::TextFormat& format(VM& vm, const Val& self)
{
    return hostOf<TextFormatHost>(vm, self, "TextFormat").format;
}

/// A field a script changed: a timeline's field shows what it has now.
void changed(display::TextField& f)
{
    f.invalidate();
}

template <class T>
Val optionalValue(const std::optional<T>& value);

template <>
Val optionalValue(const std::optional<std::string>& value)
{
    return value ? text(*value) : Val::null();
}

template <>
Val optionalValue(const std::optional<double>& value)
{
    return value ? number(*value) : Val::null();
}

template <>
Val optionalValue(const std::optional<uint32_t>& value)
{
    return value ? number(*value) : Val::null();
}

template <>
Val optionalValue(const std::optional<bool>& value)
{
    return value ? boolean(*value) : Val::null();
}

void assign(VM& vm, std::optional<std::string>& out, const Val& value)
{
    out = value.isNullish() ? std::nullopt : std::optional<std::string>(vm.toString(value));
}

void assign(VM& vm, std::optional<double>& out, const Val& value)
{
    out = value.isNullish() ? std::nullopt : std::optional<double>(vm.toNumber(value));
}

void assign(VM&, std::optional<uint32_t>& out, const Val& value)
{
    out = value.isNullish() ? std::nullopt : std::optional<uint32_t>(value.toUint32() & 0xffffff);
}

void assign(VM&, std::optional<bool>& out, const Val& value)
{
    out = value.isNullish() ? std::nullopt : std::optional<bool>(value.toBoolean());
}

template <class T>
std::pair<as3::NativeMethod, as3::NativeMethod> formatProperty(std::optional<T> display::TextFormat::*member)
{
    return {NATIVE(member) { return optionalValue(format(vm, self).*member); },
        NATIVE(member) {
            assign(vm, format(vm, self).*member, args[0]);
            return Val();
        }};
}

} // namespace

Val Bindings::newTextFormat(const display::TextFormat& value)
{
    auto host = std::make_shared<TextFormatHost>();
    host->format = value;
    return instance("flash.text.TextFormat", host);
}

void Bindings::defineText()
{
    // What flash.text.StyleSheet (resources/as3/flash/text) reads CSS with:
    // each selector and its properties (camelCase) in the order written, or
    // null when it does not parse; a color (#rrggbb, else 0); a font-family
    // list as a font.
    vm.defineFunction("flash.text.__parseCSS", NATIVE() {
        std::vector<CssRule> rules;
        if (!parseCss(args.empty() ? std::string() : vm.toString(args[0]), rules))
            return Val::null();
        std::vector<Val> out;
        for (const CssRule& rule : rules)
        {
            Val properties = vm.newObject();
            for (const auto& [name, value] : rule.properties)
                vm.setProperty(properties, camelCase(name), text(value));
            out.push_back(text(rule.selector));
            out.push_back(properties);
        }
        return vm.newArray(std::move(out));
    }, 1);
    vm.defineFunction("flash.text.__parseColor", NATIVE() {
        std::string color = args.empty() ? std::string() : vm.toString(args[0]);
        if (color.empty() || color[0] != '#')
            return number(0);
        color = color.substr(1);
        while (!color.empty() && std::isspace(static_cast<unsigned char>(color.back())))
            color.pop_back();
        if (color.empty() || color.size() > 6 || !std::all_of(color.begin(), color.end(), [](char c) {
                return std::isxdigit(static_cast<unsigned char>(c)) != 0;
            }))
            return number(0);
        return number(static_cast<double>(std::strtoul(color.c_str(), nullptr, 16)));
    }, 1);
    vm.defineFunction("flash.text.__fontList", NATIVE() {
        return text(fontList(args.empty() ? std::string() : vm.toString(args[0])));
    }, 1);

    const auto constants = [this](const char* name, std::initializer_list<std::pair<const char*, const char*>> values) {
        NativeClassBuilder builder = vm.defineNativeClass(name);
        for (const auto& [constant, value] : values)
            builder.constant(constant, text(value));
        builder.final().define();
    };
    constants("flash.text.TextFieldType", {{"DYNAMIC", "dynamic"}, {"INPUT", "input"}});
    constants("flash.text.TextFieldAutoSize", {{"NONE", "none"}, {"LEFT", "left"}, {"CENTER", "center"}, {"RIGHT", "right"}});
    constants("flash.text.TextFormatAlign", {{"LEFT", "left"}, {"CENTER", "center"}, {"RIGHT", "right"},
                                                {"JUSTIFY", "justify"}, {"START", "start"}, {"END", "end"}});
    constants("flash.text.AntiAliasType", {{"ADVANCED", "advanced"}, {"NORMAL", "normal"}});
    constants("flash.text.GridFitType", {{"NONE", "none"}, {"PIXEL", "pixel"}, {"SUBPIXEL", "subpixel"}});
    constants("flash.text.FontStyle",
        {{"REGULAR", "regular"}, {"BOLD", "bold"}, {"ITALIC", "italic"}, {"BOLD_ITALIC", "boldItalic"}});
    constants("flash.text.FontType", {{"EMBEDDED", "embedded"}, {"EMBEDDED_CFF", "embeddedCFF"}, {"DEVICE", "device"}});
    constants("flash.text.TextFormatDisplay", {{"BLOCK", "block"}, {"INLINE", "inline"}});

    // TextFormat.
    const auto font = formatProperty(&display::TextFormat::font);
    const auto size = formatProperty(&display::TextFormat::size);
    const auto color = formatProperty(&display::TextFormat::color);
    const auto bold = formatProperty(&display::TextFormat::bold);
    const auto italic = formatProperty(&display::TextFormat::italic);
    const auto underline = formatProperty(&display::TextFormat::underline);
    const auto url = formatProperty(&display::TextFormat::url);
    const auto target = formatProperty(&display::TextFormat::target);
    const auto align = formatProperty(&display::TextFormat::align);
    const auto leftMargin = formatProperty(&display::TextFormat::leftMargin);
    const auto rightMargin = formatProperty(&display::TextFormat::rightMargin);
    const auto indent = formatProperty(&display::TextFormat::indent);
    const auto leading = formatProperty(&display::TextFormat::leading);
    const auto letterSpacing = formatProperty(&display::TextFormat::letterSpacing);
    const auto blockIndent = formatProperty(&display::TextFormat::blockIndent);
    const auto kerning = formatProperty(&display::TextFormat::kerning);
    const auto bullet = formatProperty(&display::TextFormat::bullet);
    vm.defineNativeClass("flash.text.TextFormat")
        .constructor(NATIVE() {
            if (self.host<TextFormatHost>())
                return Val();
            auto host = std::make_shared<TextFormatHost>();
            display::TextFormat& f = host->format;
            assign(vm, f.font, args[0]);
            assign(vm, f.size, args[1]);
            assign(vm, f.color, args[2]);
            assign(vm, f.bold, args[3]);
            assign(vm, f.italic, args[4]);
            assign(vm, f.underline, args[5]);
            assign(vm, f.url, args[6]);
            assign(vm, f.target, args[7]);
            assign(vm, f.align, args[8]);
            assign(vm, f.leftMargin, args[9]);
            assign(vm, f.rightMargin, args[10]);
            assign(vm, f.indent, args[11]);
            assign(vm, f.leading, args[12]);
            self->host = host;
            return Val();
        })
        .property("font", font.first, font.second)
        .property("size", size.first, size.second)
        .property("color", color.first, color.second)
        .property("bold", bold.first, bold.second)
        .property("italic", italic.first, italic.second)
        .property("underline", underline.first, underline.second)
        .property("url", url.first, url.second)
        .property("target", target.first, target.second)
        .property("align", align.first, align.second)
        .property("leftMargin", leftMargin.first, leftMargin.second)
        .property("rightMargin", rightMargin.first, rightMargin.second)
        .property("indent", indent.first, indent.second)
        .property("leading", leading.first, leading.second)
        .property("letterSpacing", letterSpacing.first, letterSpacing.second)
        .property("blockIndent", blockIndent.first, blockIndent.second)
        .property("kerning", kerning.first, kerning.second)
        .property("bullet", bullet.first, bullet.second)
        .field("tabStops", Val::null())
        .field("display", text("block"))
        .define();

    // TextLineMetrics.
    vm.defineNativeClass("flash.text.TextLineMetrics")
        .field("x", number(0), as3::Coerce::Number)
        .field("width", number(0), as3::Coerce::Number)
        .field("height", number(0), as3::Coerce::Number)
        .field("ascent", number(0), as3::Coerce::Number)
        .field("descent", number(0), as3::Coerce::Number)
        .field("leading", number(0), as3::Coerce::Number)
        .constructor(NATIVE() {
            const char* names[] = {"x", "width", "height", "ascent", "descent", "leading"};
            for (size_t i = 0; i < 6; ++i)
                vm.setProperty(self, names[i], number(numberArg(vm, args, i)));
            return Val();
        })
        .define();

    // TextField.
    const auto flag = [](bool (display::TextField::*get)() const, void (display::TextField::*set)(bool)) {
        return std::make_pair(NATIVE(get) { return boolean((field(vm, self).*get)()); },
            NATIVE(set) {
                display::TextField& f = field(vm, self);
                (f.*set)(args[0].toBoolean());
                changed(f);
                return Val();
            });
    };
    const auto member = [](bool display::TextField::*member) {
        return std::make_pair(NATIVE(member) { return boolean(field(vm, self).*member); },
            NATIVE(member) {
                field(vm, self).*member = args[0].toBoolean();
                return Val();
            });
    };
    const auto multiline = flag(&display::TextField::multiline, &display::TextField::setMultiline);
    const auto wordWrap = flag(&display::TextField::wordWrap, &display::TextField::setWordWrap);
    const auto border = flag(&display::TextField::border, &display::TextField::setBorder);
    const auto background = flag(&display::TextField::background, &display::TextField::setBackground);
    const auto password = flag(&display::TextField::displayAsPassword, &display::TextField::setDisplayAsPassword);
    const auto selectable = member(&display::TextField::selectable);
    const auto wheel = member(&display::TextField::mouseWheelEnabled);
    const auto condense = member(&display::TextField::condenseWhite);
    const auto embed = member(&display::TextField::embedFonts);
    const auto alwaysShow = member(&display::TextField::alwaysShowSelection);
    const auto ignore = NATIVE() { return Val(); };
    vm.defineNativeClass("flash.text.TextField")
        .extends("flash.display.InteractiveObject")
        .constructor(NATIVE(this) {
            if (!self.host<DisplayHost>())
                attach(self, std::make_shared<display::TextField>());
            return Val();
        })
        .property("text", NATIVE() { return text(field(vm, self).text()); }, NATIVE() {
            display::TextField& f = field(vm, self);
            if (args[0].isNullish())
                vm.throwError("TypeError", "Error #2007: Parameter text must be non-null.", 2007);
            f.setText(vm.toString(args[0]));
            changed(f);
            return Val();
        })
        .property("htmlText", NATIVE() { return text(field(vm, self).htmlText()); }, NATIVE(this) {
            display::TextField& f = field(vm, self);
            // A style sheet's styles as they are now.
            if (const Val* sheet = self->properties.lookup("$styleSheet"); sheet && sheet->isObject())
                f.setStyles(stylesOf(*sheet));
            f.setHtmlText(args[0].isNullish() ? std::string() : vm.toString(args[0]));
            changed(f);
            return Val();
        })
        .getter("length", NATIVE() { return number(field(vm, self).length()); })
        .method("appendText", NATIVE() {
            display::TextField& f = field(vm, self);
            f.appendText(stringArg(vm, args, 0));
            changed(f);
            return Val();
        }, 1)
        .method("replaceText", NATIVE() {
            display::TextField& f = field(vm, self);
            f.replaceText(intArg(args, 0), intArg(args, 1), stringArg(vm, args, 2));
            changed(f);
            return Val();
        }, 3)
        .method("replaceSelectedText", NATIVE() {
            display::TextField& f = field(vm, self);
            f.replaceSelectedText(stringArg(vm, args, 0));
            changed(f);
            return Val();
        }, 1)
        .method("getRawText", NATIVE() { return text(field(vm, self).text()); })
        .property("defaultTextFormat", NATIVE(this) { return newTextFormat(field(vm, self).defaultTextFormat()); },
            NATIVE() {
                field(vm, self).setDefaultTextFormat(format(vm, args[0]));
                return Val();
            })
        .method("getTextFormat", NATIVE(this) {
            return newTextFormat(field(vm, self).getTextFormat(intArg(args, 0, -1), intArg(args, 1, -1)));
        })
        .method("setTextFormat", NATIVE() {
            display::TextField& f = field(vm, self);
            f.setTextFormat(format(vm, args[0]), intArg(args, 1, -1), intArg(args, 2, -1));
            changed(f);
            return Val();
        }, 1)
        .property("textColor", NATIVE() { return number(field(vm, self).textColor()); }, NATIVE() {
            display::TextField& f = field(vm, self);
            f.setTextColor(args[0].toUint32() & 0xffffff);
            changed(f);
            return Val();
        })
        .property("type", NATIVE() {
            return text(field(vm, self).type() == display::TextField::Type::Input ? "input" : "dynamic");
        }, NATIVE() {
            const std::string type = vm.toString(args[0]);
            if (type != "input" && type != "dynamic")
                vm.throwError("ArgumentError", "Error #2008: Parameter type must be one of the accepted values.", 2008);
            field(vm, self).setType(type == "input" ? display::TextField::Type::Input : display::TextField::Type::Dynamic);
            return Val();
        })
        .property("autoSize", NATIVE() {
            switch (field(vm, self).autoSize())
            {
            case display::TextField::AutoSize::Left:
                return text("left");
            case display::TextField::AutoSize::Center:
                return text("center");
            case display::TextField::AutoSize::Right:
                return text("right");
            default:
                return text("none");
            }
        }, NATIVE() {
            const std::string size = vm.toString(args[0]);
            display::TextField& f = field(vm, self);
            f.setAutoSize(size == "left"   ? display::TextField::AutoSize::Left
                    : size == "center"     ? display::TextField::AutoSize::Center
                    : size == "right"      ? display::TextField::AutoSize::Right
                                           : display::TextField::AutoSize::None);
            changed(f);
            return Val();
        })
        .property("multiline", multiline.first, multiline.second)
        .property("wordWrap", wordWrap.first, wordWrap.second)
        .property("border", border.first, border.second)
        .property("background", background.first, background.second)
        .property("displayAsPassword", password.first, password.second)
        .property("selectable", selectable.first, selectable.second)
        .property("mouseWheelEnabled", wheel.first, wheel.second)
        .property("condenseWhite", condense.first, condense.second)
        .property("embedFonts", embed.first, embed.second)
        .property("alwaysShowSelection", alwaysShow.first, alwaysShow.second)
        .property("borderColor", NATIVE() { return number(field(vm, self).borderColor()); }, NATIVE() {
            field(vm, self).setBorderColor(args[0].toUint32() & 0xffffff);
            return Val();
        })
        .property("backgroundColor", NATIVE() { return number(field(vm, self).backgroundColor()); }, NATIVE() {
            field(vm, self).setBackgroundColor(args[0].toUint32() & 0xffffff);
            return Val();
        })
        .property("maxChars", NATIVE() { return number(field(vm, self).maxChars); }, NATIVE() {
            field(vm, self).maxChars = std::max(0, args[0].toInt32());
            return Val();
        })
        .property("restrict", NATIVE() {
            const std::string& allowed = field(vm, self).restrictTo;
            return allowed.empty() ? Val::null() : text(allowed);
        }, NATIVE() {
            field(vm, self).restrictTo = args[0].isNullish() ? std::string() : vm.toString(args[0]);
            return Val();
        })
        .getter("numLines", NATIVE() { return number(field(vm, self).numLines()); })
        .getter("textWidth", NATIVE() { return number(field(vm, self).textWidth()); })
        .getter("textHeight", NATIVE() { return number(field(vm, self).textHeight()); })
        .property("scrollV", NATIVE() { return number(field(vm, self).scrollV()); }, NATIVE() {
            field(vm, self).setScrollV(args[0].toInt32());
            return Val();
        })
        .property("scrollH", NATIVE() { return number(field(vm, self).scrollH()); }, NATIVE() {
            field(vm, self).setScrollH(vm.toNumber(args[0]));
            return Val();
        })
        .getter("maxScrollV", NATIVE() { return number(field(vm, self).maxScrollV()); })
        .getter("maxScrollH", NATIVE() { return number(field(vm, self).maxScrollH()); })
        .getter("bottomScrollV", NATIVE() { return number(field(vm, self).bottomScrollV()); })
        .getter("caretIndex", NATIVE() { return number(field(vm, self).caretIndex()); })
        .getter("selectionBeginIndex", NATIVE() { return number(field(vm, self).selectionBeginIndex()); })
        .getter("selectionEndIndex", NATIVE() { return number(field(vm, self).selectionEndIndex()); })
        .method("setSelection", NATIVE() {
            field(vm, self).setSelection(intArg(args, 0), intArg(args, 1));
            return Val();
        }, 2)
        .method("getLineText", NATIVE() {
            display::TextField& f = field(vm, self);
            const int line = intArg(args, 0);
            if (line < 0 || line >= f.numLines())
                vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
            return text(f.getLineText(line));
        }, 1)
        .method("getLineLength", NATIVE() { return number(field(vm, self).getLineLength(intArg(args, 0))); }, 1)
        .method("getLineOffset", NATIVE() { return number(field(vm, self).getLineOffset(intArg(args, 0))); }, 1)
        .method("getLineIndexOfChar", NATIVE() {
            return number(field(vm, self).getLineIndexOfChar(intArg(args, 0)));
        }, 1)
        .method("getLineIndexAtPoint", NATIVE() {
            return number(field(vm, self).getLineIndexAtPoint(numberArg(vm, args, 0), numberArg(vm, args, 1)));
        }, 2)
        .method("getCharIndexAtPoint", NATIVE() {
            return number(field(vm, self).getCharIndexAtPoint(numberArg(vm, args, 0), numberArg(vm, args, 1)));
        }, 2)
        .method("getCharBoundaries", NATIVE(this) {
            const display::Rect box = field(vm, self).getCharBoundaries(intArg(args, 0));
            return box.width <= 0.0 && box.height <= 0.0 ? Val::null() : newRectangle(box);
        }, 1)
        .method("getLineMetrics", NATIVE() {
            display::TextField& f = field(vm, self);
            const int line = intArg(args, 0);
            const std::vector<display::TextLine>& lines = f.lines();
            if (line < 0 || line >= static_cast<int>(lines.size()))
                vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
            const display::TextLine& l = lines[static_cast<size_t>(line)];
            return vm.construct(vm.findDefinition("flash.text.TextLineMetrics"),
                {number(l.x), number(l.width), number(l.height), number(l.ascent), number(l.descent), number(l.leading)});
        }, 1)
        .method("getFirstCharInParagraph", NATIVE() {
            const std::string all = field(vm, self).text();
            const std::u32string chars = display::toCodePoints(all);
            int index = std::clamp(intArg(args, 0), 0, static_cast<int>(chars.size()));
            while (index > 0 && chars[static_cast<size_t>(index - 1)] != U'\r')
                --index;
            return number(index);
        }, 1)
        .method("getParagraphLength", NATIVE() {
            const std::u32string chars = display::toCodePoints(field(vm, self).text());
            int start = std::clamp(intArg(args, 0), 0, static_cast<int>(chars.size()));
            while (start > 0 && chars[static_cast<size_t>(start - 1)] != U'\r')
                --start;
            int end = start;
            while (end < static_cast<int>(chars.size()) && chars[static_cast<size_t>(end)] != U'\r')
                ++end;
            if (end < static_cast<int>(chars.size()))
                ++end;
            return number(end - start);
        }, 1)
        .property("antiAliasType", NATIVE() { return text("normal"); }, ignore)
        .property("gridFitType", NATIVE() { return text("pixel"); }, ignore)
        .property("sharpness", NATIVE() { return number(0); }, ignore)
        .property("thickness", NATIVE() { return number(0); }, ignore)
        // The runs of characters of one format each, as TextRuns (a method
        // the player has without saying so).
        .method("getTextRuns", NATIVE(this) {
            display::TextField& f = field(vm, self);
            const int length = f.length();
            const int begin = std::max(0, given(args, 0) ? intArg(args, 0) : 0);
            const int end = std::min(length, given(args, 1) ? intArg(args, 1) : length);
            std::vector<Val> runs;
            const Val runClass = vm.classValue(classNamed("flash.text.TextRun").get());
            for (int at = begin; at < end;)
            {
                int next = at + 1;
                while (next < end && f.formatAt(next) == f.formatAt(at))
                    ++next;
                runs.push_back(vm.construct(runClass,
                    std::vector<Val>{number(at), number(next), newTextFormat(f.formatAt(at))}));
                at = next;
            }
            return vm.newArray(std::move(runs));
        })
        .property("styleSheet", NATIVE() {
            const Val* sheet = self->properties.lookup("$styleSheet");
            return sheet ? *sheet : Val::null();
        }, NATIVE(this) {
            self->properties["$styleSheet"] = args[0];
            display::TextField& f = field(vm, self);
            if (args[0].isObject())
                f.setStyles(stylesOf(args[0]));
            else
                f.setStyles(std::nullopt);
            return Val();
        })
        .property("useRichTextClipboard", NATIVE() { return boolean(false); }, ignore)
        .getter("textSnapshot", NATIVE() { return Val::null(); })
        .define();

    // Font: the device's, minimally.
    vm.defineNativeClass("flash.text.Font")
        .field("fontName", Val::null())
        .field("fontStyle", text("regular"))
        .field("fontType", text("device"))
        .method("hasGlyphs", NATIVE() { return boolean(true); }, 1)
        .staticMethod("enumerateFonts", NATIVE() {
            std::vector<Val> fonts;
            if (booleanArg(args, 0))
                for (const char* name : {"Arial", "Courier New", "Times New Roman", "Verdana"})
                {
                    Val font = vm.construct(vm.findDefinition("flash.text.Font"));
                    vm.setProperty(font, "fontName", text(name));
                    fonts.push_back(font);
                }
            return vm.newArray(std::move(fonts));
        })
        .staticMethod("registerFont", NATIVE() { return Val(); }, 1)
        .define();
}

} // namespace script
} // namespace play
} // namespace fla
