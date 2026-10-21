#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Symbol;

/// Slice guides beyond 9-slice scaling's four: a movie clip can have any
/// number of guides across and down, and the strips between them take
/// turns, the first keeping its size, the next stretching, the next keeping
/// its size again, and so on. Two guides each way is 9-slice scaling; four
/// across keep a tab in the middle of a panel's top edge at its size while
/// the edge either side of it stretches. The first and last guide each way
/// are the symbol's 9-slice guides, so other apps scale it by those; the
/// whole list is kept in phoenixSlicesX and phoenixSlicesY. Free of Qt.
namespace slices {

extern const char* const kAcross;
extern const char* const kDown;

/// \a symbol's guides across (x) and down (y), in its own space, in order:
/// its 9-slice guides when it has no more.
std::vector<double> across(const Symbol& symbol);
std::vector<double> down(const Symbol& symbol);

/// Whether \a symbol has more than 9-slice scaling's guides.
bool more(const Symbol& symbol);

/// Sets \a symbol's guides (each list sorted, at least two): its 9-slice
/// guides become the first and last, and the rest are kept only when
/// there are more than two. One undo step named \a name.
CommandPtr set(Symbol& symbol, std::vector<double> across, std::vector<double> down, const std::string& name);

/// \a count guides spread evenly over \a low to \a high, the strips taking
/// turns as the guides do: \a count guides make \a count + 1 strips of a
/// width each.
std::vector<double> spread(double low, double high, int count);

/// Text and back for the attributes: "10,20,30".
std::string encode(const std::vector<double>& guides);
std::vector<double> decode(const std::string& text);

} // namespace slices

} // namespace fla
