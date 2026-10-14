// Persistent data: named values scripts keep on elements, library items and
// the document. Each is kept as the file keeps it, a persistentData child
// holding a PD (name, type, value) for each, so it is saved with the
// document in every format and read back as Animate wrote it.

#include "extensions.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/element.h"
#include "../data/symbol.h"
#include "../edit/property_command.h"
#include "../third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace jsfl {

namespace {

/// One value: its name, its type as the file spells it ("i" integer, "I"
/// integer array, "d" double, "D" double array, "s" string, "b" byte
/// array) and its value as text, arrays with commas between the numbers.
struct Datum
{
    std::string name;
    std::string type;
    std::string value;
};

const char* const kTag = "persistentData";

bool isData(const std::string& xml)
{
    const size_t start = xml.find_first_not_of(" \t\r\n");
    return start != std::string::npos && xml.compare(start, 15, "<persistentData") == 0 &&
           (xml.size() == start + 15 || !std::isalnum(static_cast<unsigned char>(xml[start + 15])));
}

std::vector<Datum> parse(const std::string& xml)
{
    std::vector<Datum> data;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !document.RootElement())
        return data;
    for (const tinyxml2::XMLElement* pd = document.RootElement()->FirstChildElement("PD"); pd; pd = pd->NextSiblingElement("PD"))
    {
        Datum datum;
        datum.name = pd->Attribute("n") ? pd->Attribute("n") : "";
        datum.type = pd->Attribute("t") ? pd->Attribute("t") : "s";
        datum.value = pd->Attribute("v") ? pd->Attribute("v") : "";
        data.push_back(datum);
    }
    return data;
}

std::string escape(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\n': out += "&#10;"; break;
        case '\r': out += "&#13;"; break;
        case '\t': out += "&#9;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string write(const std::vector<Datum>& data)
{
    std::string xml = "<persistentData>";
    for (const Datum& datum : data)
        xml += "<PD n=\"" + escape(datum.name) + "\" t=\"" + escape(datum.type) + "\" v=\"" + escape(datum.value) + "\"/>";
    return xml + "</persistentData>";
}

/// The data kept among an object's unmodeled children.
std::vector<Datum> dataIn(const std::vector<std::string>& children)
{
    for (const std::string& xml : children)
    {
        if (isData(xml))
            return parse(xml);
    }
    return {};
}

/// \a children with \a data in place of what they kept; no data takes the
/// child out.
std::vector<std::string> withData(std::vector<std::string> children, const std::vector<Datum>& data)
{
    const auto found = std::find_if(children.begin(), children.end(), isData);
    if (data.empty())
    {
        if (found != children.end())
            children.erase(found);
    }
    else if (found != children.end())
        *found = write(data);
    else
        children.push_back(write(data));
    return children;
}

std::string number(double value)
{
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15)
        return std::to_string(static_cast<long long>(value));
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

/// The script's value in the file's form for \a type, as addData takes it.
Datum datumFrom(const Args& args, size_t at)
{
    Datum datum;
    datum.name = arg(args, at).toString();
    if (datum.name.empty())
        throw Error("Data needs a name.");
    const std::string type = arg(args, at + 1).toString();
    const Value& value = arg(args, at + 2);
    const auto list = [&value](bool whole) {
        std::string text;
        const std::vector<Value> items = value.type == Value::Type::Array ? value.items : std::vector<Value>{value};
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i)
                text += ",";
            const double n = items[i].toNumber();
            text += number(whole ? std::trunc(n) : n);
        }
        return text;
    };
    if (type == "integer")
    {
        datum.type = "i";
        datum.value = number(std::trunc(value.toNumber()));
    }
    else if (type == "integerArray")
    {
        datum.type = "I";
        datum.value = list(true);
    }
    else if (type == "double")
    {
        datum.type = "d";
        datum.value = number(value.toNumber());
    }
    else if (type == "doubleArray")
    {
        datum.type = "D";
        datum.value = list(false);
    }
    else if (type == "byteArray")
    {
        datum.type = "b";
        datum.value = list(true);
    }
    else if (type == "string")
    {
        datum.type = "s";
        datum.value = value.isNullish() ? std::string() : value.toString();
    }
    else
        throw Error("Data is integer, integerArray, double, doubleArray, string or byteArray, not " + type + ".");
    return datum;
}

Value valueOf(const Datum& datum)
{
    const auto numbers = [&datum]() {
        Value list = Value::array();
        size_t start = 0;
        while (start < datum.value.size())
        {
            size_t end = datum.value.find(',', start);
            if (end == std::string::npos)
                end = datum.value.size();
            list.items.emplace_back(std::atof(datum.value.substr(start, end - start).c_str()));
            start = end + 1;
        }
        return list;
    };
    if (datum.type == "i" || datum.type == "d")
        return Value(std::atof(datum.value.c_str()));
    if (datum.type == "I" || datum.type == "D" || datum.type == "b")
        return numbers();
    return Value(datum.value);
}

Value lookup(const std::vector<Datum>& data, const Args& args)
{
    const std::string name = arg(args, 0).toString();
    for (const Datum& datum : data)
    {
        if (datum.name == name)
            return valueOf(datum);
    }
    return Value();
}

bool contains(const std::vector<Datum>& data, const Args& args)
{
    const std::string name = arg(args, 0).toString();
    return std::any_of(data.begin(), data.end(), [&name](const Datum& datum) { return datum.name == name; });
}

std::vector<Datum> setDatum(std::vector<Datum> data, const Datum& datum)
{
    const auto found = std::find_if(data.begin(), data.end(), [&datum](const Datum& d) { return d.name == datum.name; });
    if (found != data.end())
        *found = datum;
    else
        data.push_back(datum);
    return data;
}

std::vector<Datum> removeDatum(std::vector<Datum> data, const std::string& name)
{
    data.erase(std::remove_if(data.begin(), data.end(), [&name](const Datum& d) { return d.name == name; }), data.end());
    return data;
}

/// Changes the data kept among \a children, as one step of the script's
/// undo.
void change(DocumentPart& part, std::vector<std::string>& children, const std::vector<Datum>& data)
{
    const std::vector<std::string> after = withData(children, data);
    if (after == children)
        return;
    part.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(children, children, after, "Persistent Data")), false);
}

// The document's data, kept among its raw sections.

std::vector<Datum> documentData(Document& d)
{
    for (const fla::Document::RawSection& section : d.data().extraSections)
    {
        if (isData(section.xml))
            return parse(section.xml);
    }
    return {};
}

void setDocumentData(Document& d, const std::vector<Datum>& data)
{
    fla::Document& document = d.data();
    std::vector<fla::Document::RawSection> after = document.extraSections;
    const auto found = std::find_if(after.begin(), after.end(), [](const fla::Document::RawSection& s) { return isData(s.xml); });
    if (data.empty())
    {
        if (found == after.end())
            return;
        after.erase(found);
    }
    else if (found != after.end())
        found->xml = write(data);
    else
        after.push_back({write(data), false});
    d.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::Document::RawSection>>(document.extraSections,
              document.extraSections, after, "Persistent Data")),
        false);
}

/// The library item's unmodeled children, where its data is kept.
std::vector<std::string>& itemChildren(Item& i)
{
    if (i.kind == Item::Kind::Symbol)
        return i.symbol()->extraChildren;
    if (i.kind == Item::Kind::Bitmap)
        return i.bitmap()->extraChildren;
    throw Error("Phoenix keeps data on symbols and bitmaps, not on " +
                std::string(i.kind == Item::Kind::Folder ? "folders" : i.kind == Item::Kind::Sound ? "sounds" : "videos") + ".");
}

} // namespace

void describeElementData(ClassDef<Element>& def)
{
    def.method("setPersistentData", 3, [](Element& e, const Args& args) {
        fla::Element& element = e.get();
        change(e, element.extraChildren, setDatum(dataIn(element.extraChildren), datumFrom(args, 0)));
        return Value();
    });
    def.method("getPersistentData", 1, [](Element& e, const Args& args) { return lookup(dataIn(e.get().extraChildren), args); });
    def.method("hasPersistentData", 1, [](Element& e, const Args& args) { return Value(contains(dataIn(e.get().extraChildren), args)); });
    def.method("removePersistentData", 1, [](Element& e, const Args& args) {
        fla::Element& element = e.get();
        change(e, element.extraChildren, removeDatum(dataIn(element.extraChildren), arg(args, 0).toString()));
        return Value();
    });
}

void describeItemData(ClassDef<Item>& def)
{
    def.method("addData", 3, [](Item& i, const Args& args) {
        std::vector<std::string>& children = itemChildren(i);
        change(i, children, setDatum(dataIn(children), datumFrom(args, 0)));
        return Value();
    });
    def.method("getData", 1, [](Item& i, const Args& args) { return lookup(dataIn(itemChildren(i)), args); });
    def.method("hasData", 1, [](Item& i, const Args& args) { return Value(contains(dataIn(itemChildren(i)), args)); });
    def.method("removeData", 1, [](Item& i, const Args& args) {
        std::vector<std::string>& children = itemChildren(i);
        change(i, children, removeDatum(dataIn(children), arg(args, 0).toString()));
        return Value();
    });
}

void describeDocumentData(ClassDef<Document>& def)
{
    def.method("addDataToDocument", 3, [](Document& d, const Args& args) {
        setDocumentData(d, setDatum(documentData(d), datumFrom(args, 0)));
        return Value();
    });
    def.method("getDataFromDocument", 1, [](Document& d, const Args& args) { return lookup(documentData(d), args); });
    def.method("documentHasData", 1, [](Document& d, const Args& args) { return Value(contains(documentData(d), args)); });
    def.method("removeDataFromDocument", 1, [](Document& d, const Args& args) {
        setDocumentData(d, removeDatum(documentData(d), arg(args, 0).toString()));
        return Value();
    });
    // The selected elements each keep a copy.
    def.method("addDataToSelection", 3, [](Document& d, const Args& args) {
        const Datum datum = datumFrom(args, 0);
        for (fla::Element* element : selectedElements(d.session, d.document))
        {
            Element wrapper(d.session, d.document, element);
            change(wrapper, element->extraChildren, setDatum(dataIn(element->extraChildren), datum));
        }
        return Value();
    });
    def.method("removeDataFromSelection", 1, [](Document& d, const Args& args) {
        for (fla::Element* element : selectedElements(d.session, d.document))
        {
            Element wrapper(d.session, d.document, element);
            change(wrapper, element->extraChildren, removeDatum(dataIn(element->extraChildren), arg(args, 0).toString()));
        }
        return Value();
    });
}

} // namespace jsfl
