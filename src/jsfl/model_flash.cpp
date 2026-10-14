// `fl` and its output panel; colors and the wrappers the model's files share.

#include "flfile.h"
#include "model.h"
#include "xul.h"

#include "../data/fla_document.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace jsfl {

std::string colorText(const uint8_t rgba[4])
{
    char buffer[16];
    if (rgba[3] == 255)
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0], rgba[1], rgba[2]);
    else
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", rgba[0], rgba[1], rgba[2], rgba[3]);
    return buffer;
}

bool parseColor(const Value& value, uint8_t rgba[4])
{
    if (value.type == Value::Type::Number)
    {
        const auto number = static_cast<unsigned long>(value.toNumber());
        rgba[0] = static_cast<uint8_t>((number >> 16) & 0xFF);
        rgba[1] = static_cast<uint8_t>((number >> 8) & 0xFF);
        rgba[2] = static_cast<uint8_t>(number & 0xFF);
        rgba[3] = 255;
        return true;
    }
    std::string text = value.toString();
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }),
        text.end());
    if (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0)
        text = "#" + text.substr(2);
    if (text.empty() || text[0] != '#')
        return false;
    std::string hex = text.substr(1);
    if (!std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        return false;
    if (hex.size() == 3)
        hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    if (hex.size() != 6 && hex.size() != 8)
        return false;
    const auto byte = [&hex](size_t at) { return static_cast<uint8_t>(std::stoul(hex.substr(at, 2), nullptr, 16)); };
    rgba[0] = byte(0);
    rgba[1] = byte(2);
    rgba[2] = byte(4);
    rgba[3] = hex.size() == 8 ? byte(6) : 255;
    return true;
}

void DocumentPart::require(const fla::DOMElement* object, const char* what) const
{
    data();
    if (!session.alive(document, object))
        throw Error(std::string("This ") + what + " is no longer in the document (it was deleted, or an edit replaced it).");
}

std::shared_ptr<Document> wrapDocument(Session& session, fla::FLADocument* document)
{
    if (!document)
        return nullptr;
    return std::make_shared<Document>(session, document);
}

namespace {

fla::FLADocument* documentOf(Flash& fl, const Value& value)
{
    if (value.isNullish())
    {
        fla::FLADocument* active = fl.session.host().activeDocument();
        if (!active)
            throw Error("No document is open.");
        return active;
    }
    const auto document = value.as<Document>();
    if (!document)
        throw Error("A Document is needed.");
    document->data();
    return document->document;
}

std::string withSlash(std::string folder)
{
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\')
#ifdef _WIN32
        folder += '\\';
#else
        folder += '/';
#endif
    return folder;
}

Value saveAs(Flash& fl, fla::FLADocument* document, const std::string& path, bool copy)
{
    Session& session = fl.session;
    session.beforeSwitch();
    std::string error;
    if (!session.host().save(document, path, copy, error))
        throw Error(error.empty() ? "The document could not be saved." : error);
    return Value(true);
}

} // namespace

const ClassInfo& Flash::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Flash> def(info, "Flash");
        def.get("documents", [](Flash& fl) {
            Value list = Value::array();
            for (fla::FLADocument* document : fl.session.host().documents())
                list.items.emplace_back(wrapDocument(fl.session, document));
            return list;
        });
        def.method("getDocumentDOM", 0, [](Flash& fl, const Args&) {
            return Value(wrapDocument(fl.session, fl.session.host().activeDocument()));
        });
        def.method("openDocument", 1, [](Flash& fl, const Args& args) {
            const std::string path = uri::toPath(arg(args, 0).toString());
            fl.session.beforeSwitch();
            std::string error;
            fla::FLADocument* document = fl.session.host().open(path, error);
            if (!document)
                throw Error(error.empty() ? "Could not open " + path : error);
            return Value(wrapDocument(fl.session, document));
        });
        def.method("createDocument", 1, [](Flash& fl, const Args& args) {
            const std::string type = given(args, 0) ? arg(args, 0).toString() : "timeline";
            if (type != "timeline")
                throw Error("Phoenix makes timeline documents only, not " + type + ".");
            fl.session.beforeSwitch();
            std::string error;
            fla::FLADocument* document = fl.session.host().create(error);
            if (!document)
                throw Error(error.empty() ? "A new document could not be made." : error);
            return Value(wrapDocument(fl.session, document));
        });
        def.method("saveDocument", 2, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            std::string path = given(args, 1) ? uri::toPath(arg(args, 1).toString()) : std::string();
            if (path.empty() && fl.session.host().path(document).empty())
            {
                path = fl.session.host().browseForFile("save", "Save Document");
                if (path.empty())
                    return Value(false);
            }
            return saveAs(fl, document, path, false);
        });
        def.method("saveDocumentAs", 1, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            const std::string path = fl.session.host().browseForFile("save", "Save Document As");
            if (path.empty())
                return Value(false);
            return saveAs(fl, document, path, false);
        });
        def.method("saveAll", 0, [](Flash& fl, const Args&) {
            for (fla::FLADocument* document : fl.session.host().documents())
            {
                if (!fl.session.host().path(document).empty() && fl.session.host().modified(document))
                    saveAs(fl, document, std::string(), false);
            }
            return Value();
        });
        def.method("closeDocument", 2, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            const bool ask = given(args, 1) ? arg(args, 1).toBool() : true;
            fl.session.beforeSwitch();
            const bool closed = fl.session.host().close(document, ask);
            if (closed)
                fl.session.forget(document);
            return Value(closed);
        });
        def.method("closeAll", 1, [](Flash& fl, const Args& args) {
            const bool ask = given(args, 0) ? arg(args, 0).toBool() : true;
            fl.session.beforeSwitch();
            bool all = true;
            for (fla::FLADocument* document : fl.session.host().documents())
            {
                if (fl.session.host().close(document, ask))
                    fl.session.forget(document);
                else
                    all = false;
            }
            return Value(all);
        });
        def.method("setActiveWindow", 1, [](Flash& fl, const Args& args) {
            fla::FLADocument* document = documentOf(fl, arg(args, 0));
            if (document != fl.session.host().activeDocument())
            {
                fl.session.beforeSwitch();
                fl.session.host().activate(document);
            }
            return Value();
        });
        def.method("findDocumentIndex", 1, [](Flash& fl, const Args& args) {
            const std::string name = arg(args, 0).toString();
            Value found = Value::array();
            const std::vector<fla::FLADocument*> documents = fl.session.host().documents();
            for (size_t i = 0; i < documents.size(); ++i)
            {
                if (fl.session.host().name(documents[i]) == name)
                    found.items.emplace_back(static_cast<int>(i));
            }
            return found;
        });
        def.method("trace", 1, [](Flash& fl, const Args& args) {
            fl.session.host().trace(arg(args, 0).toString());
            return Value();
        });
        def.get("configDirectory", [](Flash& fl) { return Value(withSlash(fl.session.host().configDirectory())); });
        def.get("configURI", [](Flash& fl) {
            std::string folder = uri::fromPath(fl.session.host().configDirectory());
            if (folder.empty() || folder.back() != '/')
                folder += '/';
            return Value(folder);
        });
        def.get("scriptURI", [](Flash& fl) {
            return fl.session.scriptPath.empty() ? Value("") : Value(uri::fromPath(fl.session.scriptPath));
        });
        def.get("version", [](Flash& fl) {
            // The scripting level Phoenix aims at, first, as scripts read it
            // (fl.version.split(" ")[1]), then Phoenix's own.
#ifdef _WIN32
            std::string text = "WIN 24,0,0,0";
#else
            std::string text = "MAC 24,0,0,0";
#endif
            return Value(text + " Phoenix " + fl.session.host().version());
        });
        def.method("runScript", 2, [](Flash& fl, const Args& args) {
            if (!fl.runScript)
                throw Error("Scripts cannot run other scripts here.");
            const std::string path = uri::toPath(arg(args, 0).toString());
            const std::string function = given(args, 1) ? arg(args, 1).toString() : std::string();
            const Args rest(args.size() > 2 ? args.begin() + 2 : args.end(), args.end());
            return fl.runScript(path, function, rest);
        });
        def.method("browseForFileURL", 2, [](Flash& fl, const Args& args) {
            const std::string mode = given(args, 0) ? arg(args, 0).toString() : "open";
            const std::string path = fl.session.host().browseForFile(mode, arg(args, 1).isNullish() ? "" : arg(args, 1).toString());
            return path.empty() ? Value::null() : Value(uri::fromPath(path));
        });
        def.method("browseForFolderURL", 1, [](Flash& fl, const Args& args) {
            const std::string path = fl.session.host().browseForFolder(arg(args, 0).isNullish() ? "" : arg(args, 0).toString());
            return path.empty() ? Value::null() : Value(uri::fromPath(path));
        });
        def.get("outputPanel", [](Flash& fl) { return Value(std::make_shared<OutputPanel>(fl.session)); });
        def.method("showIdleMessage", 1, [](Flash&, const Args&) { return Value(); });
        def.method("clipCopyString", 1, [](Flash& fl, const Args& args) {
            fl.session.host().copyText(arg(args, 0).toString());
            return Value();
        });
        def.method("closeAllPlayerDocuments", 0, [](Flash&, const Args&) { return Value(true); });
        def.method("xmlPanel", 1, [](Flash& fl, const Args& args) {
            return showXmlPanelFile(fl.session, arg(args, 0).toString());
        });
        def.method("xmlPanelFromString", 1, [](Flash& fl, const Args& args) {
            return showXmlPanel(fl.session, arg(args, 0).toString());
        });
        def.get("xmlui", [](Flash& fl) { return Value(makeXmlUI(fl.session)); });
        return info;
    }();
    return info;
}

const ClassInfo& OutputPanel::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<OutputPanel> def(info, "outputPanel");
        def.method("trace", 1, [](OutputPanel& panel, const Args& args) {
            panel.session.host().trace(arg(args, 0).toString());
            return Value();
        });
        def.method("clear", 0, [](OutputPanel& panel, const Args&) {
            panel.session.host().clearOutput();
            return Value();
        });
        def.method("save", 3, [](OutputPanel& panel, const Args& args) {
            const std::string path = uri::toPath(arg(args, 0).toString());
            const bool append = arg(args, 1).toBool();
            std::ofstream out(std::filesystem::u8path(path), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            const std::string text = panel.session.host().outputText();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return Value(static_cast<bool>(out));
        });
        return info;
    }();
    return info;
}

std::shared_ptr<Flash> makeFlash(Session& session)
{
    return std::make_shared<Flash>(session);
}

} // namespace jsfl
