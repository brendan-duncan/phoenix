#include "as2_library.h"

namespace fla {
namespace play {

const std::string& as2LibrarySource()
{
    static const unsigned char bytes[] = {
#include "as2_library.inc"
        0};
    static const std::string source(reinterpret_cast<const char*>(bytes), sizeof(bytes) - 1);
    return source;
}

} // namespace play
} // namespace fla
