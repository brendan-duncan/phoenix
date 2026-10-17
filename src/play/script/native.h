#pragma once

// What the bind_*.cpp files share to define their classes briefly.

#include "bindings.h"

#include <cmath>
#include <cstring>
#include <string>

/// A native method, getter or setter: (vm, self, args) in scope, what it
/// captures given.
#define NATIVE(...) [__VA_ARGS__](as3::VM & vm, const as3::Val& self, as3::Args args) -> as3::Val

namespace fla {
namespace play {
namespace script {

using as3::Args;
using as3::NativeClassBuilder;
using as3::Val;
using as3::VM;

inline Val number(double value)
{
    return Val::number(value);
}

inline Val boolean(bool value)
{
    return Val::boolean(value);
}

inline Val text(const std::string& value)
{
    return Val::string(value);
}

/// An argument given and not undefined.
inline bool given(const Args& args, size_t i)
{
    return i < args.size() && !args[i].isUndefined();
}

/// An argument as a number, \a otherwise when it is missing.
inline double numberArg(VM& vm, const Args& args, size_t i, double otherwise = 0.0)
{
    return given(args, i) ? vm.toNumber(args[i]) : otherwise;
}

inline bool booleanArg(const Args& args, size_t i, bool otherwise = false)
{
    return given(args, i) ? args[i].toBoolean() : otherwise;
}

inline std::string stringArg(VM& vm, const Args& args, size_t i, const std::string& otherwise = std::string())
{
    return given(args, i) && !args[i].isNull() ? vm.toString(args[i]) : otherwise;
}

inline uint32_t uintArg(const Args& args, size_t i, uint32_t otherwise = 0)
{
    return given(args, i) ? args[i].toUint32() : otherwise;
}

inline int32_t intArg(const Args& args, size_t i, int32_t otherwise = 0)
{
    return given(args, i) ? args[i].toInt32() : otherwise;
}

/// A number as ActionScript writes it.
inline std::string numberText(double value)
{
    return as3::numberToString(value);
}

} // namespace script
} // namespace play
} // namespace fla
