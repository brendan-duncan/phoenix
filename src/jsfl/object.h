#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/// Phoenix's JSFL object model: the objects scripts reach through `fl`
/// (documents, timelines, layers, frames, elements, the library), as plain
/// C++ over the document model. Nothing here knows the script engine; the
/// QuickJS binding (engine.cpp) maps each class onto JavaScript, and other
/// languages can map the same tables.
///
/// Free of Qt, so the tests and the command line can use it without a window.
namespace jsfl {

class Object;
using ObjectPtr = std::shared_ptr<Object>;

/// A failure reported to the script as an Error, in words for its author.
struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// A value passed between a script and the object model: what JavaScript
/// can hold, with the model's objects as references.
struct Value
{
    enum class Type
    {
        Undefined,
        Null,
        Boolean,
        Number,
        String,
        Array,
        /// A plain script object ({x: 1, y: 2}), its fields in order.
        Map,
        /// One of the model's objects.
        Object,
        /// A date, as milliseconds since 1970.
        Date
    };

    Type type = Type::Undefined;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> fields;
    ObjectPtr object;

    Value() = default;
    Value(bool value) : type(Type::Boolean), boolean(value) {}
    Value(int value) : type(Type::Number), number(value) {}
    Value(double value) : type(Type::Number), number(value) {}
    Value(const char* value) : type(Type::String), string(value) {}
    Value(std::string value) : type(Type::String), string(std::move(value)) {}
    /// A model object, or null for none.
    template <class T, class = std::enable_if_t<std::is_base_of<Object, T>::value>>
    Value(std::shared_ptr<T> value)
        : type(value ? Type::Object : Type::Null)
        , object(std::move(value))
    {}

    static Value null()
    {
        Value value;
        value.type = Type::Null;
        return value;
    }
    static Value array(std::vector<Value> items = {})
    {
        Value value;
        value.type = Type::Array;
        value.items = std::move(items);
        return value;
    }
    static Value map()
    {
        Value value;
        value.type = Type::Map;
        return value;
    }
    static Value date(double milliseconds)
    {
        Value value;
        value.type = Type::Date;
        value.number = milliseconds;
        return value;
    }
    /// An array of numbers or strings, as many getters return.
    template <class T>
    static Value list(const std::vector<T>& values)
    {
        Value value = array();
        for (const T& each : values)
            value.items.emplace_back(each);
        return value;
    }

    bool isUndefined() const { return type == Type::Undefined; }
    bool isNullish() const { return type == Type::Undefined || type == Type::Null; }

    /// As JavaScript converts: numbers from numeric strings and booleans,
    /// truth from anything.
    double toNumber() const;
    int toInt() const;
    bool toBool() const;
    std::string toString() const;

    /// A field of a Map, or null.
    const Value* field(const std::string& name) const;
    Value& set(const std::string& name, Value value);

    /// The model object, when it is a \a T.
    template <class T>
    std::shared_ptr<T> as() const
    {
        return std::dynamic_pointer_cast<T>(object);
    }
};

using Args = std::vector<Value>;

/// The argument at \a index, or undefined past the end.
inline const Value& arg(const Args& args, size_t index)
{
    static const Value none;
    return index < args.size() ? args[index] : none;
}
inline bool given(const Args& args, size_t index)
{
    return index < args.size() && !args[index].isUndefined();
}

struct ClassInfo;

/// What scripts see of the model: each object belongs to a class, which
/// lists its properties and methods.
class Object
{
public:
    virtual ~Object() = default;
    virtual const ClassInfo& classInfo() const = 0;
    /// What the object stands for, so the same thing reached twice is the
    /// same script object (fl.documents[0] == fl.getDocumentDOM()); null
    /// for objects made fresh each time.
    virtual const void* identity() const { return nullptr; }
    /// What the identity is within (its document), as an object of a
    /// closed document can share an address with one of a new document.
    virtual const void* scope() const { return nullptr; }
};

struct Property
{
    std::string name;
    std::function<Value(Object&)> get;
    /// Empty for a read-only property.
    std::function<void(Object&, const Value&)> set;
};

struct Method
{
    std::string name;
    int length = 0;
    std::function<Value(Object&, const Args&)> call;
};

struct ClassInfo
{
    std::string name;
    const ClassInfo* parent = nullptr;
    std::vector<Property> properties;
    std::vector<Method> methods;
};

namespace detail {

template <class T>
T fromValue(const Value& value);
template <>
inline double fromValue<double>(const Value& value) { return value.toNumber(); }
template <>
inline int fromValue<int>(const Value& value) { return value.toInt(); }
template <>
inline bool fromValue<bool>(const Value& value) { return value.toBool(); }
template <>
inline std::string fromValue<std::string>(const Value& value) { return value.toString(); }
template <>
inline Value fromValue<Value>(const Value& value) { return value; }

} // namespace detail

/// Declares a class's properties and methods, as short as a table:
///
///     ClassDef<Layer> def(info, "Layer");
///     def.prop("name", &Layer::name, &Layer::setName)
///        .get("frameCount", &Layer::frameCount)
///        .method("getFrame", 1, [](Layer& layer, const Args& args) { ... });
template <class T>
class ClassDef
{
public:
    ClassDef(ClassInfo& info, const char* name, const ClassInfo* parent = nullptr)
        : _info(info)
    {
        _info.name = name;
        _info.parent = parent;
    }

    /// A read-only property from a lambda.
    ClassDef& get(const char* name, std::function<Value(T&)> getter)
    {
        _info.properties.push_back({name, [getter](Object& o) { return getter(static_cast<T&>(o)); }, {}});
        return *this;
    }
    /// A read-only property from a member function.
    template <class R>
    ClassDef& get(const char* name, R (T::*getter)() const)
    {
        return get(name, [getter](T& self) { return Value((self.*getter)()); });
    }

    /// A read-write property from lambdas.
    ClassDef& prop(const char* name, std::function<Value(T&)> getter, std::function<void(T&, const Value&)> setter)
    {
        _info.properties.push_back({name, [getter](Object& o) { return getter(static_cast<T&>(o)); },
            [setter](Object& o, const Value& v) { setter(static_cast<T&>(o), v); }});
        return *this;
    }
    /// A read-write property from member functions; the value is converted
    /// to what the setter takes.
    template <class R, class A>
    ClassDef& prop(const char* name, R (T::*getter)() const, void (T::*setter)(A))
    {
        using Plain = std::decay_t<A>;
        return prop(name, [getter](T& self) { return Value((self.*getter)()); },
            [setter](T& self, const Value& v) { (self.*setter)(detail::fromValue<Plain>(v)); });
    }

    ClassDef& method(const char* name, int length, std::function<Value(T&, const Args&)> call)
    {
        _info.methods.push_back({name, length, [call](Object& o, const Args& args) { return call(static_cast<T&>(o), args); }});
        return *this;
    }

private:
    ClassInfo& _info;
};

} // namespace jsfl
