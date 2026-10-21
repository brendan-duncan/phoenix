// AMF3 and AMF0, written and read as Flash Player has them: references to
// strings, objects and traits seen before in the same value (AMF3), class
// aliases (registerClassAlias) naming typed objects, and IExternalizable
// classes writing themselves.

#include "amf.h"

#include "bindings.h"

#include "bytecode/vm.h"
#include "runtime/value.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace fla {
namespace play {
namespace script {

using as3::Val;
using as3::VM;

namespace {

enum Amf3 : uint8_t
{
    Undefined3 = 0x00,
    Null3 = 0x01,
    False3 = 0x02,
    True3 = 0x03,
    Integer3 = 0x04,
    Double3 = 0x05,
    String3 = 0x06,
    XmlDoc3 = 0x07,
    Date3 = 0x08,
    Array3 = 0x09,
    Object3 = 0x0A,
    Xml3 = 0x0B,
    ByteArray3 = 0x0C,
    VectorInt3 = 0x0D,
    VectorUint3 = 0x0E,
    VectorDouble3 = 0x0F,
    VectorObject3 = 0x10,
    Dictionary3 = 0x11
};

enum Amf0 : uint8_t
{
    Number0 = 0x00,
    Boolean0 = 0x01,
    String0 = 0x02,
    Object0 = 0x03,
    Null0 = 0x05,
    Undefined0 = 0x06,
    Reference0 = 0x07,
    EcmaArray0 = 0x08,
    ObjectEnd0 = 0x09,
    StrictArray0 = 0x0A,
    Date0 = 0x0B,
    LongString0 = 0x0C,
    XmlDoc0 = 0x0F,
    TypedObject0 = 0x10,
    Avmplus0 = 0x11
};

const std::string& className(const Val& value)
{
    static const std::string none;
    return value.isObject() && value->cls ? value->cls->name : none;
}

bool isClass(const Val& value, const char* name)
{
    return className(value) == name;
}

/// Whether \a index names an element ("0", "12").
bool isIndexName(const std::string& name)
{
    if (name.empty() || name.size() > 10 || (name.size() > 1 && name[0] == '0'))
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// The members a class instance is written with: its public variables and
/// properties with both a getter and a setter, in the order declared.
std::vector<std::string> sealedMembers(const as3::ClassInfo* cls)
{
    std::vector<std::pair<uint64_t, std::string>> found;
    if (!cls)
        return {};
    for (const auto& [name, trait] : cls->traits)
    {
        if (name.empty() || name[0] == '$' || name.find("::") != std::string::npos || !trait.isPublic)
            continue;
        if (trait.kind == as3::TraitKind::Slot && !trait.isConst)
            found.emplace_back(trait.slot, name);
        else if (trait.kind == as3::TraitKind::Accessor && trait.getter.isObject() && trait.setter.isObject())
            found.emplace_back(uint64_t(1) << 40, name);
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (auto& entry : found)
        out.push_back(std::move(entry.second));
    return out;
}

bool externalizable(AmfContext& b, const as3::ClassInfo* cls)
{
    const std::shared_ptr<as3::ClassInfo> face = b.classNamed("flash.utils.IExternalizable");
    return cls && face && cls->isSubclassOf(face.get());
}

/// What a value's dynamic properties are, in the order for-in visits them.
std::vector<std::string> dynamicNames(VM& vm, const Val& object)
{
    std::vector<std::string> names;
    if (!object.isObject())
        return names;
    for (const auto& property : object->properties)
        names.push_back(property.first);
    (void)vm;
    return names;
}

/// A class's value, or undefined when there is no such class.
Val classValueOf(AmfContext& b, const std::string& name)
{
    const std::shared_ptr<as3::ClassInfo> cls = b.classNamed(name);
    return cls ? b.vm.classValue(cls.get()) : Val();
}

class Writer
{
public:
    Writer(AmfContext& b, std::vector<uint8_t>& out)
        : b(b)
        , vm(b.vm)
        , out(out)
    {}

    // ---- AMF3

    void u29(uint32_t value)
    {
        value &= 0x1fffffff;
        if (value < 0x80)
            out.push_back(uint8_t(value));
        else if (value < 0x4000)
        {
            out.push_back(uint8_t(0x80 | (value >> 7)));
            out.push_back(uint8_t(value & 0x7f));
        }
        else if (value < 0x200000)
        {
            out.push_back(uint8_t(0x80 | (value >> 14)));
            out.push_back(uint8_t(0x80 | ((value >> 7) & 0x7f)));
            out.push_back(uint8_t(value & 0x7f));
        }
        else
        {
            out.push_back(uint8_t(0x80 | (value >> 22)));
            out.push_back(uint8_t(0x80 | ((value >> 15) & 0x7f)));
            out.push_back(uint8_t(0x80 | ((value >> 8) & 0x7f)));
            out.push_back(uint8_t(value & 0xff));
        }
    }
    void bigEndian(uint64_t value, int bytes)
    {
        for (int i = bytes - 1; i >= 0; --i)
            out.push_back(uint8_t(value >> (i * 8)));
    }
    void doubleValue(double value)
    {
        uint64_t bits;
        std::memcpy(&bits, &value, 8);
        bigEndian(bits, 8);
    }
    void string3(const std::string& text)
    {
        if (text.empty())
        {
            u29(1);
            return;
        }
        const auto found = strings.find(text);
        if (found != strings.end())
        {
            u29(uint32_t(found->second) << 1);
            return;
        }
        strings.emplace(text, int(strings.size()));
        u29((uint32_t(text.size()) << 1) | 1);
        out.insert(out.end(), text.begin(), text.end());
    }
    /// A reference to an object written before, or false and the object
    /// counted from here on.
    bool reference3(const Val& value)
    {
        const auto found = objects.find(value.get());
        if (found != objects.end())
        {
            u29(uint32_t(found->second) << 1);
            return true;
        }
        objects.emplace(value.get(), int(objects.size()));
        return false;
    }

    void value3(const Val& value)
    {
        if (value.isUndefined())
            out.push_back(Undefined3);
        else if (value.isNull())
            out.push_back(Null3);
        else if (value.isBoolean())
            out.push_back(value.toBoolean() ? True3 : False3);
        else if (value.isNumber())
        {
            const double n = value.toNumber();
            if (n == std::floor(n) && n >= -268435456.0 && n <= 268435455.0 && !(n == 0 && std::signbit(n)))
            {
                out.push_back(Integer3);
                u29(uint32_t(int32_t(n)));
            }
            else
            {
                out.push_back(Double3);
                doubleValue(n);
            }
        }
        else if (value.isString())
        {
            out.push_back(String3);
            string3(value.str());
        }
        else if (value.isFunction() || isClass(value, "Function"))
            // As the player has it: a function is nothing to write.
            out.push_back(Undefined3);
        else if (value.isObject())
            object3(value);
        else
            out.push_back(Undefined3);
    }

    void object3(const Val& value)
    {
        const std::string& cls = className(value);
        if (cls == "XML" || cls == "XMLList")
        {
            out.push_back(Xml3);
            if (reference3(value))
                return;
            const std::string text = vm.toString(vm.callMethod(value, "toXMLString", as3::Args()));
            u29((uint32_t(text.size()) << 1) | 1);
            out.insert(out.end(), text.begin(), text.end());
            return;
        }
        if (cls == "Date")
        {
            out.push_back(Date3);
            if (reference3(value))
                return;
            u29(1);
            doubleValue(vm.toNumber(vm.callMethod(value, "getTime", as3::Args())));
            return;
        }
        if (const ByteArrayHost* bytes = byteArrayOf(value))
        {
            out.push_back(ByteArray3);
            if (reference3(value))
                return;
            const std::vector<uint8_t> copy = bytes->bytes;
            u29((uint32_t(copy.size()) << 1) | 1);
            out.insert(out.end(), copy.begin(), copy.end());
            return;
        }
        if (value->cls && value->cls->isVector)
        {
            vector3(value);
            return;
        }
        if (value->type == as3::ValueType::Array)
        {
            out.push_back(Array3);
            if (reference3(value))
                return;
            const std::vector<Val> elements = value->elements;
            u29((uint32_t(elements.size()) << 1) | 1);
            for (const std::string& name : dynamicNames(vm, value))
            {
                if (isIndexName(name))
                    continue;
                string3(name);
                value3(vm.getProperty(value, name));
            }
            string3(std::string());
            for (const Val& element : elements)
                value3(element);
            return;
        }
        if (cls == "flash.utils.Dictionary")
        {
            out.push_back(Dictionary3);
            if (reference3(value))
                return;
            const Val keys = vm.makeIterator(value, false);
            const std::vector<Val> list = keys.isObject() ? keys->elements : std::vector<Val>();
            u29((uint32_t(list.size()) << 1) | 1);
            out.push_back(0); // weak keys
            for (const Val& key : list)
            {
                value3(key);
                value3(vm.getIndex(value, key));
            }
            return;
        }
        out.push_back(Object3);
        if (reference3(value))
            return;
        const as3::ClassInfo* info = value->cls.get();
        const bool plain = !info || info == vm.builtins().object.get();
        const std::string alias = plain ? std::string() : b.aliasOf(info);
        const bool external = !plain && externalizable(b, info);
        const std::vector<std::string> sealed = plain || external ? std::vector<std::string>() : sealedMembers(info);
        const bool dynamic = plain || (info && info->isDynamic);
        // Traits: written once for each class name and member list.
        std::string traitKey = alias + "\n" + (external ? "!" : "") + (dynamic ? "*" : "");
        for (const std::string& name : sealed)
            traitKey += "\n" + name;
        const auto found = traits.find(traitKey);
        if (found != traits.end())
            u29((uint32_t(found->second) << 2) | 1);
        else
        {
            traits.emplace(traitKey, int(traits.size()));
            u29((uint32_t(sealed.size()) << 4) | (dynamic ? 0x08 : 0) | (external ? 0x04 : 0) | 0x03);
            string3(alias);
            for (const std::string& name : sealed)
                string3(name);
        }
        if (external)
        {
            const Val output = b.newByteArray({});
            if (!output.isObject())
                return;
            vm.callMethod(value, "writeExternal", {output});
            if (const ByteArrayHost* written = byteArrayOf(output))
                out.insert(out.end(), written->bytes.begin(), written->bytes.end());
            return;
        }
        for (const std::string& name : sealed)
            value3(vm.getProperty(value, name));
        if (dynamic)
        {
            for (const std::string& name : dynamicNames(vm, value))
            {
                const Val member = vm.getProperty(value, name);
                if (member.isFunction())
                    continue;
                string3(name);
                value3(member);
            }
            string3(std::string());
        }
    }

    void vector3(const Val& value)
    {
        const as3::ClassInfo* cls = value->cls.get();
        const std::string element = cls->elementClass ? cls->elementClass->name : std::string("*");
        const uint8_t marker = element == "int" ? VectorInt3 : element == "uint" ? VectorUint3
            : element == "Number"                                             ? VectorDouble3
                                                                              : VectorObject3;
        out.push_back(marker);
        if (reference3(value))
            return;
        const std::vector<Val> elements = value->elements;
        u29((uint32_t(elements.size()) << 1) | 1);
        out.push_back(value->boolValue ? 1 : 0);
        if (marker == VectorObject3)
            string3(element == "*" || element == "Object" ? std::string() : element);
        for (const Val& item : elements)
        {
            if (marker == VectorInt3)
                bigEndian(uint32_t(as3::toInt32(item.toNumber())), 4);
            else if (marker == VectorUint3)
                bigEndian(as3::toUint32(item.toNumber()), 4);
            else if (marker == VectorDouble3)
                doubleValue(item.toNumber());
            else
                value3(item);
        }
    }

    // ---- AMF0

    void string0(const std::string& text, bool marker)
    {
        if (text.size() > 0xffff)
        {
            if (marker)
                out.push_back(LongString0);
            bigEndian(text.size(), 4);
        }
        else
        {
            if (marker)
                out.push_back(String0);
            bigEndian(text.size(), 2);
        }
        out.insert(out.end(), text.begin(), text.end());
    }

    void value0(const Val& value)
    {
        if (value.isUndefined() || value.isFunction() || isClass(value, "Function"))
            out.push_back(Undefined0);
        else if (value.isNull())
            out.push_back(Null0);
        else if (value.isBoolean())
        {
            out.push_back(Boolean0);
            out.push_back(value.toBoolean() ? 1 : 0);
        }
        else if (value.isNumber())
        {
            out.push_back(Number0);
            doubleValue(value.toNumber());
        }
        else if (value.isString())
            string0(value.str(), true);
        else if (value.isObject())
            object0(value);
        else
            out.push_back(Undefined0);
    }

    void object0(const Val& value)
    {
        const std::string& cls = className(value);
        if (cls == "Date")
        {
            out.push_back(Date0);
            doubleValue(vm.toNumber(vm.callMethod(value, "getTime", as3::Args())));
            bigEndian(0, 2);
            return;
        }
        if (cls == "XML" || cls == "XMLList")
        {
            out.push_back(XmlDoc0);
            const std::string text = vm.toString(vm.callMethod(value, "toXMLString", as3::Args()));
            bigEndian(text.size(), 4);
            out.insert(out.end(), text.begin(), text.end());
            return;
        }
        // What only AMF3 can say, said in AMF3.
        if (byteArrayOf(value) || (value->cls && value->cls->isVector) || cls == "flash.utils.Dictionary")
        {
            out.push_back(Avmplus0);
            value3(value);
            return;
        }
        const auto seen = objects0.find(value.get());
        if (seen != objects0.end())
        {
            out.push_back(Reference0);
            bigEndian(uint32_t(seen->second), 2);
            return;
        }
        objects0.emplace(value.get(), int(objects0.size()));
        if (value->type == as3::ValueType::Array)
        {
            const std::vector<Val> elements = value->elements;
            std::vector<std::string> named;
            for (const std::string& name : dynamicNames(vm, value))
                if (!isIndexName(name))
                    named.push_back(name);
            if (named.empty())
            {
                out.push_back(StrictArray0);
                bigEndian(elements.size(), 4);
                for (const Val& element : elements)
                    value0(element);
                return;
            }
            out.push_back(EcmaArray0);
            bigEndian(elements.size(), 4);
            for (size_t i = 0; i < elements.size(); ++i)
            {
                string0(std::to_string(i), false);
                value0(elements[i]);
            }
            for (const std::string& name : named)
            {
                string0(name, false);
                value0(vm.getProperty(value, name));
            }
            bigEndian(0, 2);
            out.push_back(ObjectEnd0);
            return;
        }
        const as3::ClassInfo* info = value->cls.get();
        const bool plain = !info || info == vm.builtins().object.get();
        const std::string alias = plain ? std::string() : b.aliasOf(info);
        if (!alias.empty())
        {
            out.push_back(TypedObject0);
            string0(alias, false);
        }
        else
            out.push_back(Object0);
        std::vector<std::string> names = plain ? std::vector<std::string>() : sealedMembers(info);
        for (const std::string& name : dynamicNames(vm, value))
            names.push_back(name);
        for (const std::string& name : names)
        {
            const Val member = vm.getProperty(value, name);
            if (member.isFunction())
                continue;
            string0(name, false);
            value0(member);
        }
        bigEndian(0, 2);
        out.push_back(ObjectEnd0);
    }

private:
    AmfContext& b;
    VM& vm;
    std::vector<uint8_t>& out;
    std::map<std::string, int> strings;
    std::map<const as3::Value*, int> objects;
    std::map<std::string, int> traits;
    std::map<const as3::Value*, int> objects0;
};

class Reader
{
public:
    Reader(AmfContext& b, const std::vector<uint8_t>& in, size_t& at)
        : b(b)
        , vm(b.vm)
        , in(in)
        , at(at)
    {}

    [[noreturn]] void endOfFile()
    {
        vm.throwError("flash.errors.EOFError", "Error #2030: End of file was encountered.", 2030);
    }
    uint8_t byte()
    {
        if (at >= in.size())
            endOfFile();
        return in[at++];
    }
    uint64_t bigEndian(int bytes)
    {
        uint64_t value = 0;
        for (int i = 0; i < bytes; ++i)
            value = (value << 8) | byte();
        return value;
    }
    double doubleValue()
    {
        const uint64_t bits = bigEndian(8);
        double value;
        std::memcpy(&value, &bits, 8);
        return value;
    }
    std::string bytesText(size_t length)
    {
        if (at + length > in.size())
            endOfFile();
        std::string text(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + length));
        at += length;
        return text;
    }

    // ---- AMF3

    uint32_t u29()
    {
        uint32_t value = 0;
        for (int i = 0; i < 3; ++i)
        {
            const uint8_t b = byte();
            if (!(b & 0x80))
                return (value << 7) | b;
            value = (value << 7) | (b & 0x7f);
        }
        return (value << 8) | byte();
    }
    std::string string3()
    {
        const uint32_t header = u29();
        if (!(header & 1))
        {
            if ((header >> 1) >= strings.size())
                badReference();
            return strings[header >> 1];
        }
        const std::string text = bytesText(header >> 1);
        if (!text.empty())
            strings.push_back(text);
        return text;
    }
    [[noreturn]] void badReference()
    {
        vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
    }
    /// The object a reference header names, or none (an inline one).
    bool reference3(uint32_t header, Val& out)
    {
        if (header & 1)
            return false;
        if ((header >> 1) >= objects.size())
            badReference();
        out = objects[header >> 1];
        return true;
    }

    Val value3()
    {
        const uint8_t marker = byte();
        switch (marker)
        {
        case Undefined3:
            return Val();
        case Null3:
            return Val::null();
        case False3:
            return Val::boolean(false);
        case True3:
            return Val::boolean(true);
        case Integer3:
        {
            uint32_t value = u29();
            if (value & 0x10000000)
                value |= 0xe0000000;
            return Val::number(double(int32_t(value)));
        }
        case Double3:
            return Val::number(doubleValue());
        case String3:
            return Val::string(string3());
        case Xml3:
        case XmlDoc3:
        {
            const uint32_t header = u29();
            Val found;
            if (reference3(header, found))
                return found;
            const std::string text = bytesText(header >> 1);
            const Val made = vm.construct(classValueOf(b, "XML"), std::vector<Val>{Val::string(text)});
            objects.push_back(made);
            return made;
        }
        case Date3:
        {
            const uint32_t header = u29();
            Val found;
            if (reference3(header, found))
                return found;
            const Val made =
                vm.construct(classValueOf(b, "Date"), std::vector<Val>{Val::number(doubleValue())});
            objects.push_back(made);
            return made;
        }
        case Array3:
        {
            const uint32_t header = u29();
            Val found;
            if (reference3(header, found))
                return found;
            Val made = vm.newArray({});
            objects.push_back(made);
            // Named elements first (an index among them an element).
            for (std::string name = string3(); !name.empty(); name = string3())
            {
                const Val item = value3();
                if (isIndexName(name))
                    vm.setIndex(made, Val::number(std::stod(name)), item);
                else
                    vm.setProperty(made, name, item);
            }
            const uint32_t count = header >> 1;
            for (uint32_t i = 0; i < count; ++i)
                vm.setIndex(made, Val::number(i), value3());
            return made;
        }
        case ByteArray3:
        {
            const uint32_t header = u29();
            Val found;
            if (reference3(header, found))
                return found;
            const std::string bytes = bytesText(header >> 1);
            const Val made = b.newByteArray(std::vector<uint8_t>(bytes.begin(), bytes.end()));
            objects.push_back(made);
            return made;
        }
        case VectorInt3:
        case VectorUint3:
        case VectorDouble3:
        case VectorObject3:
            return vector3(marker);
        case Dictionary3:
        {
            const uint32_t header = u29();
            Val found;
            if (reference3(header, found))
                return found;
            byte(); // weak keys
            const Val made = vm.construct(classValueOf(b, "flash.utils.Dictionary"));
            objects.push_back(made);
            for (uint32_t i = 0; i < (header >> 1); ++i)
            {
                const Val key = value3();
                vm.setIndex(made, key, value3());
            }
            return made;
        }
        case Object3:
            return object3();
        default:
            vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
        }
    }

    struct Traits
    {
        std::string alias;
        bool dynamic = false;
        bool external = false;
        std::vector<std::string> sealed;
    };

    Val object3()
    {
        const uint32_t header = u29();
        Val found;
        if (reference3(header, found))
            return found;
        Traits kind;
        if (!(header & 2))
        {
            if ((header >> 2) >= traits.size())
                badReference();
            kind = traits[header >> 2];
        }
        else
        {
            kind.external = (header & 4) != 0;
            kind.dynamic = (header & 8) != 0;
            kind.alias = string3();
            for (uint32_t i = 0; i < (header >> 4); ++i)
                kind.sealed.push_back(string3());
            traits.push_back(kind);
        }
        // A class an alias names: one of its own, made without arguments.
        Val made;
        if (std::shared_ptr<as3::ClassInfo> cls = b.classOfAlias(kind.alias))
            made = vm.construct(vm.classValue(cls.get()));
        else
            made = vm.newObject();
        objects.push_back(made);
        if (kind.external)
        {
            std::vector<uint8_t> rest(in.begin() + std::ptrdiff_t(at), in.end());
            const Val input = b.newByteArray(std::move(rest));
            if (!input.isObject())
                return made;
            vm.callMethod(made, "readExternal", {input});
            if (const ByteArrayHost* read = byteArrayOf(input))
                at += read->position;
            return made;
        }
        for (const std::string& name : kind.sealed)
            vm.setProperty(made, name, value3());
        if (kind.dynamic)
            for (std::string name = string3(); !name.empty(); name = string3())
                vm.setProperty(made, name, value3());
        return made;
    }

    Val vector3(uint8_t marker)
    {
        const uint32_t header = u29();
        Val found;
        if (reference3(header, found))
            return found;
        const bool fixed = byte() != 0;
        std::string element = marker == VectorInt3 ? "int" : marker == VectorUint3 ? "uint"
            : marker == VectorDouble3                                              ? "Number"
                                                                                   : std::string();
        if (marker == VectorObject3)
        {
            element = string3();
            if (element.empty())
                element = "Object";
        }
        Val type = element == "Object" ? vm.classValue(vm.builtins().object.get())
                                       : (b.classNamed(element) ? vm.classValue(b.classNamed(element).get())
                                                                : vm.classValue(vm.builtins().object.get()));
        const Val cls = vm.applyType(vm.classValue(vm.builtins().vector.get()), {type});
        Val made = vm.construct(cls);
        objects.push_back(made);
        for (uint32_t i = 0; i < (header >> 1); ++i)
        {
            Val item;
            if (marker == VectorInt3)
                item = Val::number(double(int32_t(uint32_t(bigEndian(4)))));
            else if (marker == VectorUint3)
                item = Val::number(double(uint32_t(bigEndian(4))));
            else if (marker == VectorDouble3)
                item = Val::number(doubleValue());
            else
                item = value3();
            made->elements.push_back(item);
        }
        made->boolValue = fixed;
        return made;
    }

    // ---- AMF0

    std::string string0(bool longString)
    {
        return bytesText(size_t(bigEndian(longString ? 4 : 2)));
    }

    void properties0(const Val& made)
    {
        for (;;)
        {
            const std::string name = string0(false);
            if (name.empty() && at < in.size() && in[at] == ObjectEnd0)
            {
                ++at;
                return;
            }
            vm.setProperty(made, name, value0());
        }
    }

    Val value0()
    {
        const uint8_t marker = byte();
        switch (marker)
        {
        case Number0:
            return Val::number(doubleValue());
        case Boolean0:
            return Val::boolean(byte() != 0);
        case String0:
            return Val::string(string0(false));
        case LongString0:
            return Val::string(string0(true));
        case Null0:
            return Val::null();
        case Undefined0:
            return Val();
        case Reference0:
        {
            const size_t index = size_t(bigEndian(2));
            if (index >= objects0.size())
                badReference();
            return objects0[index];
        }
        case Object0:
        {
            const Val made = vm.newObject();
            objects0.push_back(made);
            properties0(made);
            return made;
        }
        case TypedObject0:
        {
            const std::string alias = string0(false);
            Val made;
            if (std::shared_ptr<as3::ClassInfo> cls = b.classOfAlias(alias))
                made = vm.construct(vm.classValue(cls.get()));
            else
                made = vm.newObject();
            objects0.push_back(made);
            properties0(made);
            return made;
        }
        case EcmaArray0:
        {
            Val made = vm.newArray({});
            objects0.push_back(made);
            bigEndian(4);
            for (;;)
            {
                const std::string name = string0(false);
                if (name.empty() && at < in.size() && in[at] == ObjectEnd0)
                {
                    ++at;
                    break;
                }
                const Val item = value0();
                if (isIndexName(name))
                    vm.setIndex(made, Val::number(std::stod(name)), item);
                else
                    vm.setProperty(made, name, item);
            }
            return made;
        }
        case StrictArray0:
        {
            Val made = vm.newArray({});
            objects0.push_back(made);
            const uint32_t count = uint32_t(bigEndian(4));
            for (uint32_t i = 0; i < count; ++i)
                vm.setIndex(made, Val::number(i), value0());
            return made;
        }
        case Date0:
        {
            const double time = doubleValue();
            bigEndian(2);
            return vm.construct(classValueOf(b, "Date"), std::vector<Val>{Val::number(time)});
        }
        case XmlDoc0:
        {
            const std::string text = string0(true);
            return vm.construct(classValueOf(b, "XML"), std::vector<Val>{Val::string(text)});
        }
        case Avmplus0:
            return value3();
        default:
            vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
        }
    }

private:
    AmfContext& b;
    VM& vm;
    const std::vector<uint8_t>& in;
    size_t& at;
    std::vector<std::string> strings;
    std::vector<Val> objects;
    std::vector<Traits> traits;
    std::vector<Val> objects0;
};

} // namespace

std::shared_ptr<as3::ClassInfo> AmfContext::classNamed(const std::string& name)
{
    try
    {
        return vm.findClass(name);
    }
    catch (as3::ScriptException&)
    {
        return nullptr;
    }
}

Val AmfContext::newByteArray(std::vector<uint8_t>)
{
    return Val();
}

std::string AmfContext::aliasOf(const as3::ClassInfo*)
{
    return std::string();
}

std::shared_ptr<as3::ClassInfo> AmfContext::classOfAlias(const std::string&)
{
    return nullptr;
}

BindingsAmf::BindingsAmf(Bindings& bindings)
    : AmfContext(bindings.vm)
    , _bindings(bindings)
{}

std::shared_ptr<as3::ClassInfo> BindingsAmf::classNamed(const std::string& name)
{
    return _bindings.classNamed(name);
}

Val BindingsAmf::newByteArray(std::vector<uint8_t> bytes)
{
    return _bindings.newByteArray(std::move(bytes));
}

std::string BindingsAmf::aliasOf(const as3::ClassInfo* cls)
{
    return _bindings.aliasOf(cls);
}

std::shared_ptr<as3::ClassInfo> BindingsAmf::classOfAlias(const std::string& alias)
{
    return _bindings.classOfAlias(alias);
}

void writeAmf(AmfContext& context, std::vector<uint8_t>& out, const Val& value, bool amf3)
{
    Writer writer(context, out);
    if (amf3)
        writer.value3(value);
    else
        writer.value0(value);
}

Val readAmf(AmfContext& context, const std::vector<uint8_t>& in, size_t& position, bool amf3)
{
    Reader reader(context, in, position);
    return amf3 ? reader.value3() : reader.value0();
}

std::vector<uint8_t> writeSharedObject(AmfContext& context, const std::string& name, const Val& data, bool amf3)
{
    // Each property: its name, its value, and a byte of 0; in AMF3 the
    // values share their references.
    std::vector<uint8_t> body;
    Writer writer(context, body);
    if (data.isObject())
    {
        for (const auto& property : data->properties)
        {
            if (property.second.isFunction())
                continue;
            if (amf3)
            {
                writer.string3(property.first);
                writer.value3(property.second);
            }
            else
            {
                writer.string0(property.first, false);
                writer.value0(property.second);
            }
            body.push_back(0);
        }
    }
    std::vector<uint8_t> file = {0x00, 0xbf, 0, 0, 0, 0, 'T', 'C', 'S', 'O', 0x00, 0x04, 0x00, 0x00, 0x00, 0x00};
    file.push_back(uint8_t(name.size() >> 8));
    file.push_back(uint8_t(name.size()));
    file.insert(file.end(), name.begin(), name.end());
    file.insert(file.end(), {0, 0, 0, uint8_t(amf3 ? 3 : 0)});
    file.insert(file.end(), body.begin(), body.end());
    const uint32_t length = uint32_t(file.size() - 6);
    for (int i = 0; i < 4; ++i)
        file[2 + i] = uint8_t(length >> (24 - 8 * i));
    return file;
}

bool readSharedObject(AmfContext& context, const std::vector<uint8_t>& file, Val& data)
{
    if (file.size() < 22 || file[0] != 0x00 || file[1] != 0xbf || std::memcmp(file.data() + 6, "TCSO", 4) != 0)
        return false;
    size_t at = 16;
    const size_t nameLength = (size_t(file[at]) << 8) | file[at + 1];
    at += 2 + nameLength + 4;
    if (at > file.size())
        return false;
    const bool amf3 = file[at - 1] == 3;
    Val made = context.vm.newObject();
    Reader reader(context, file, at);
    try
    {
        while (at < file.size())
        {
            const std::string name = amf3 ? reader.string3() : reader.string0(false);
            const Val value = amf3 ? reader.value3() : reader.value0();
            context.vm.setProperty(made, name, value);
            if (at < file.size())
                ++at;
        }
    }
    catch (as3::ScriptException&)
    {
    }
    data = made;
    return true;
}

} // namespace script
} // namespace play
} // namespace fla
