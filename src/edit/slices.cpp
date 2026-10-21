#include "slices.h"

#include "../data/symbol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace fla {

namespace slices {

const char* const kAcross = "phoenixSlicesX";
const char* const kDown = "phoenixSlicesY";

namespace {

std::string attribute(const Symbol& symbol, const char* name)
{
    for (const auto& [key, value] : symbol.extraAttributes)
        if (key == name)
            return value;
    return std::string();
}

/// The guides kept under \a name when they still start and end at the
/// 9-slice guides \a first and \a last; else just those two (another app
/// may have moved them).
std::vector<double> guides(const Symbol& symbol, const char* name, double first, double last)
{
    std::vector<double> list = decode(attribute(symbol, name));
    if (list.size() > 2 && std::abs(list.front() - first) < 0.01 && std::abs(list.back() - last) < 0.01)
        return list;
    return {first, last};
}

/// What a symbol's slicing is: the 9-slice guides and the attributes.
struct Slicing
{
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    std::vector<std::pair<std::string, std::string>> attributes;
};

Slicing slicingOf(const Symbol& symbol)
{
    return Slicing{symbol.scaleGridLeft, symbol.scaleGridTop, symbol.scaleGridRight, symbol.scaleGridBottom,
        symbol.extraAttributes};
}

class SetCommand : public Command
{
public:
    SetCommand(Symbol& symbol, Slicing after, std::string name)
        : _symbol(symbol)
        , _before(slicingOf(symbol))
        , _after(std::move(after))
        , _name(std::move(name))
    {
    }

    void redo() override { put(_after); }
    void undo() override { put(_before); }
    std::string name() const override { return _name; }

private:
    void put(const Slicing& slicing)
    {
        _symbol.scaleGridLeft = slicing.left;
        _symbol.scaleGridTop = slicing.top;
        _symbol.scaleGridRight = slicing.right;
        _symbol.scaleGridBottom = slicing.bottom;
        _symbol.extraAttributes = slicing.attributes;
    }

    Symbol& _symbol;
    Slicing _before;
    Slicing _after;
    std::string _name;
};

} // namespace

std::vector<double> across(const Symbol& symbol)
{
    return guides(symbol, kAcross, symbol.scaleGridLeft, symbol.scaleGridRight);
}

std::vector<double> down(const Symbol& symbol)
{
    return guides(symbol, kDown, symbol.scaleGridTop, symbol.scaleGridBottom);
}

bool more(const Symbol& symbol)
{
    return across(symbol).size() > 2 || down(symbol).size() > 2;
}

CommandPtr set(Symbol& symbol, std::vector<double> across, std::vector<double> down, const std::string& name)
{
    std::sort(across.begin(), across.end());
    std::sort(down.begin(), down.end());
    if (across.size() < 2 || down.size() < 2)
        return nullptr;
    Slicing after = slicingOf(symbol);
    after.left = across.front();
    after.right = across.back();
    after.top = down.front();
    after.bottom = down.back();
    auto& attributes = after.attributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) {
                             return pair.first == kAcross || pair.first == kDown;
                         }),
        attributes.end());
    if (across.size() > 2)
        attributes.emplace_back(kAcross, encode(across));
    if (down.size() > 2)
        attributes.emplace_back(kDown, encode(down));
    return CommandPtr(new SetCommand(symbol, std::move(after), name));
}

std::vector<double> spread(double low, double high, int count)
{
    std::vector<double> list;
    count = std::max(2, count);
    for (int i = 1; i <= count; ++i)
        list.push_back(std::round((low + (high - low) * i / (count + 1.0)) * 20.0) / 20.0);
    return list;
}

std::string encode(const std::vector<double>& guides)
{
    std::string text;
    for (const double guide : guides)
    {
        char number[32];
        std::snprintf(number, sizeof(number), "%.10g", guide);
        if (!text.empty())
            text += ',';
        text += number;
    }
    return text;
}

std::vector<double> decode(const std::string& text)
{
    std::vector<double> list;
    std::istringstream in(text);
    std::string part;
    while (std::getline(in, part, ','))
        if (!part.empty())
            list.push_back(std::atof(part.c_str()));
    std::sort(list.begin(), list.end());
    return list;
}

} // namespace slices

} // namespace fla
