#pragma once

// AMF, the format Flash Player writes ActionScript values in to a
// ByteArray (writeObject), a SharedObject's file and the network: AMF3, and
// the older AMF0.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace as3 {
class Val;
class VM;
}

namespace fla {
namespace play {
namespace script {

class Bindings;

/// \a value written to the end of \a out, in AMF3 or AMF0.
void writeAmf(Bindings& bindings, std::vector<uint8_t>& out, const as3::Val& value, bool amf3);
/// A value read from \a in at \a position (moved on past it); throws an
/// EOFError when the bytes run out, a RangeError for a marker it does not
/// know.
as3::Val readAmf(Bindings& bindings, const std::vector<uint8_t>& in, size_t& position, bool amf3);

} // namespace script
} // namespace play
} // namespace fla
