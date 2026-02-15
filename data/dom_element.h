#pragma once

#include <string>

namespace fla {

enum class SymbolType
{
    MovieClip,
    Button,
    Graphic
};

enum class LoopType
{
    None,
    SingleFrame,
    Loop,
    PlayOnce,
    PingPong
};

class DOMElement
{
public:
    bool visible = true;

    DOMElement();

    virtual ~DOMElement() = default;

    virtual std::string domTypeName() const = 0;
};

} // namespace fla
