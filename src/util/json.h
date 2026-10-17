#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

/// A small JSON value: enough to carry calls between a web page and
/// Phoenix's code without Qt (the HTML panels' Node.js host). Numbers are
/// doubles; object keys keep sorted order.
namespace json {

class Value
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : _type(Type::Bool), _bool(b) {}
    Value(int n) : _type(Type::Number), _number(n) {}
    Value(long long n) : _type(Type::Number), _number(static_cast<double>(n)) {}
    Value(double n) : _type(Type::Number), _number(n) {}
    Value(const char* s) : _type(Type::String), _string(s) {}
    Value(std::string s) : _type(Type::String), _string(std::move(s)) {}
    Value(Array a) : _type(Type::Array), _array(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : _type(Type::Object), _object(std::make_shared<Object>(std::move(o))) {}

    Type type() const { return _type; }
    bool isNull() const { return _type == Type::Null; }
    bool isBool() const { return _type == Type::Bool; }
    bool isNumber() const { return _type == Type::Number; }
    bool isString() const { return _type == Type::String; }
    bool isArray() const { return _type == Type::Array; }
    bool isObject() const { return _type == Type::Object; }

    bool toBool(bool fallback = false) const { return _type == Type::Bool ? _bool : fallback; }
    double toNumber(double fallback = 0) const { return _type == Type::Number ? _number : fallback; }
    int toInt(int fallback = 0) const { return _type == Type::Number ? static_cast<int>(_number) : fallback; }
    const std::string& toString() const;
    const Array& toArray() const;
    const Object& toObject() const;

    /// The member \a key of an object, or null.
    const Value& operator[](const std::string& key) const;
    /// The item \a index of an array, or null.
    const Value& operator[](size_t index) const;
    /// The member \a key, made (and this made an object) when missing.
    Value& set(const std::string& key, Value value);
    /// Adds \a value to the end (this made an array when it is not one).
    void push(Value value);

    /// Compact JSON text.
    std::string dump() const;
    /// Reads JSON text; null and \a ok false when it is not JSON.
    static Value parse(const std::string& text, bool* ok = nullptr);

private:
    void dumpTo(std::string& out) const;

    Type _type = Type::Null;
    bool _bool = false;
    double _number = 0;
    std::string _string;
    std::shared_ptr<Array> _array;
    std::shared_ptr<Object> _object;
};

/// \a text as a JSON string literal, quotes included.
std::string quote(const std::string& text);

} // namespace json
