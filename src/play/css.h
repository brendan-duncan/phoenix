#pragma once

// The CSS a StyleSheet's parseCSS reads: selectors and their properties, as
// Flash Player reads them (names with dashes made camelCase by the caller).

#include <string>
#include <utility>
#include <vector>

namespace fla {
namespace play {

struct CssRule
{
    std::string selector;
    /// Each property's name and value, in the order written.
    std::vector<std::pair<std::string, std::string>> properties;
};

/// \a text's rules, one for each selector (a list of them shares its
/// properties); false when it does not parse, nothing kept.
bool parseCss(const std::string& text, std::vector<CssRule>& rules);

/// font-size as fontSize.
std::string camelCase(const std::string& name);

/// A font-family list as a TextFormat's font: names joined by commas, the
/// generic ones as the player names them (_sans, _serif, _typewriter).
std::string fontList(const std::string& families);

} // namespace play
} // namespace fla
