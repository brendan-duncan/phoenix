#pragma once

// AMF, the format Flash Player writes ActionScript values in to a
// ByteArray (writeObject), a SharedObject's file and the network: AMF3, and
// the older AMF0.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace as3 {
class Val;
class VM;
struct ClassInfo;
}

namespace fla {
namespace play {
namespace script {

class Bindings;

/// What AMF asks of the scripts whose values it writes and reads: their
/// classes, and (in ActionScript 3) ByteArrays and the aliases classes are
/// written with.
class AmfContext
{
public:
    explicit AmfContext(as3::VM& vm)
        : vm(vm)
    {}
    virtual ~AmfContext() = default;

    as3::VM& vm;
    virtual std::shared_ptr<as3::ClassInfo> classNamed(const std::string& name);
    /// A ByteArray of \a bytes; undefined where there are none.
    virtual as3::Val newByteArray(std::vector<uint8_t> bytes);
    virtual std::string aliasOf(const as3::ClassInfo* cls);
    virtual std::shared_ptr<as3::ClassInfo> classOfAlias(const std::string& alias);

    /// ActionScript 1 and 2 write as the player writes theirs: arrays as
    /// ECMA arrays of all their names, objects' properties in for-in's
    /// order, getters as undefined, movie clips as undefined and functions
    /// left out.
    virtual bool actionScript2() const { return false; }
    virtual std::vector<std::string> namesOf(const as3::Val& object);
    virtual bool isAccessor(const as3::Val& object, const std::string& name);
    virtual bool isMovieClip(const as3::Val& value);
    /// The name Object.registerClass gave \a object's class; empty for none.
    virtual std::string registeredName(const as3::Val& object);
};

/// ActionScript 3's: its ByteArrays and registerClassAlias.
class BindingsAmf : public AmfContext
{
public:
    explicit BindingsAmf(Bindings& bindings);
    std::shared_ptr<as3::ClassInfo> classNamed(const std::string& name) override;
    as3::Val newByteArray(std::vector<uint8_t> bytes) override;
    std::string aliasOf(const as3::ClassInfo* cls) override;
    std::shared_ptr<as3::ClassInfo> classOfAlias(const std::string& alias) override;

private:
    Bindings& _bindings;
};

/// \a value written to the end of \a out, in AMF3 or AMF0.
void writeAmf(AmfContext& context, std::vector<uint8_t>& out, const as3::Val& value, bool amf3);
/// A value read from \a in at \a position (moved on past it); throws an
/// EOFError when the bytes run out, a RangeError for a marker it does not
/// know.
as3::Val readAmf(AmfContext& context, const std::vector<uint8_t>& in, size_t& position, bool amf3);

/// A shared object's file (.sol, as Flash Player keeps them): its name and
/// \a data's properties, each in AMF3 or AMF0.
std::vector<uint8_t> writeSharedObject(AmfContext& context, const std::string& name, const as3::Val& data, bool amf3);
/// The data a .sol file holds, as a new object; false when \a file is not
/// one.
bool readSharedObject(AmfContext& context, const std::vector<uint8_t>& file, as3::Val& data);

} // namespace script
} // namespace play
} // namespace fla
