#pragma once

#include "../data/filter.h"

#include <string>
#include <vector>

namespace fla {

/// Filter presets: an instance's filters as text to keep under a name and
/// put on another instance later, each filter on a line of key=value
/// pairs. Free of Qt.
namespace filterpresets {

std::string toText(const std::vector<Filter>& filters);

/// The filters \a text holds; what cannot be read is left out.
std::vector<Filter> fromText(const std::string& text);

} // namespace filterpresets

} // namespace fla
