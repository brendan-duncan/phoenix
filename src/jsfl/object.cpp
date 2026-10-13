#include "object.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace jsfl {

double Value::toNumber() const
{
    switch (type)
    {
    case Type::Boolean:
        return boolean ? 1.0 : 0.0;
    case Type::Number:
    case Type::Date:
        return number;
    case Type::Null:
        return 0.0;
    case Type::String:
    {
        const char* start = string.c_str();
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
            ++start;
        if (!*start)
            return 0.0;
        char* end = nullptr;
        const double value = (start[0] == '0' && (start[1] == 'x' || start[1] == 'X'))
            ? static_cast<double>(std::strtoll(start + 2, &end, 16))
            : std::strtod(start, &end);
        while (end && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
            ++end;
        return end && !*end ? value : std::numeric_limits<double>::quiet_NaN();
    }
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

int Value::toInt() const
{
    const double value = toNumber();
    if (!std::isfinite(value))
        return 0;
    if (value >= 2147483647.0)
        return 2147483647;
    if (value <= -2147483648.0)
        return -2147483647 - 1;
    return static_cast<int>(value);
}

bool Value::toBool() const
{
    switch (type)
    {
    case Type::Boolean:
        return boolean;
    case Type::Number:
        return number != 0.0 && !std::isnan(number);
    case Type::String:
        return !string.empty();
    case Type::Undefined:
    case Type::Null:
        return false;
    default:
        return true;
    }
}

std::string Value::toString() const
{
    switch (type)
    {
    case Type::Undefined:
        return "undefined";
    case Type::Null:
        return "null";
    case Type::Boolean:
        return boolean ? "true" : "false";
    case Type::Number:
    {
        if (std::isnan(number))
            return "NaN";
        if (number == std::floor(number) && std::fabs(number) < 1e15)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.0f", number);
            return buffer;
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.15g", number);
        return buffer;
    }
    case Type::String:
        return string;
    case Type::Array:
    {
        std::string text;
        for (size_t i = 0; i < items.size(); ++i)
            text += (i ? "," : "") + (items[i].isNullish() ? std::string() : items[i].toString());
        return text;
    }
    default:
        return "[object Object]";
    }
}

const Value* Value::field(const std::string& name) const
{
    for (const auto& entry : fields)
    {
        if (entry.first == name)
            return &entry.second;
    }
    return nullptr;
}

Value& Value::set(const std::string& name, Value value)
{
    for (auto& entry : fields)
    {
        if (entry.first == name)
        {
            entry.second = std::move(value);
            return entry.second;
        }
    }
    fields.emplace_back(name, std::move(value));
    return fields.back().second;
}

} // namespace jsfl
