#pragma once

#include <optional>
#include <set>
#include <string>

namespace fla {

/// Short next-step hints, each shown once, the first time someone does one
/// of a few key things: draws on the first frame, adds a keyframe, makes a
/// symbol, makes a tween. The GUI shows the text in a small bubble that can
/// be dismissed; which have been shown is kept between runs. Free of Qt.
class FirstUseHints
{
public:
    enum class Event
    {
        /// Art drawn with a drawing tool.
        Drew,
        /// A keyframe or blank keyframe added.
        Keyframe,
        /// A symbol made from the selection, or a new empty one.
        Symbol,
        /// A tween made: motion, classic or shape.
        Tween,
    };

    /// What was going on when the event happened.
    struct Context
    {
        /// The playhead's frame, from 0.
        int frame = 0;
        /// For a tween, "motion", "classic" or "shape".
        std::string tween;
    };

    struct Hint
    {
        /// Its name in shownText(): "first-drawing", "first-keyframe",
        /// "first-symbol", "first-tween".
        std::string id;
        std::string text;
    };

    /// The hint \a event earns, marked shown, or none when hints are off,
    /// it was shown before or the event does not call for one (drawing on a
    /// later frame, say).
    std::optional<Hint> on(Event event, const Context& context);
    // Not a default argument: a nested class's member initializers are not
    // usable in one inside the enclosing class (Clang and GCC refuse it).
    std::optional<Hint> on(Event event) { return on(event, Context()); }

    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; }
    bool wasShown(const std::string& id) const { return _shown.count(id) > 0; }
    /// Every hint shows again.
    void reset() { _shown.clear(); }

    /// The ids of the hints shown, parted by spaces, and read back.
    std::string shownText() const;
    void setShownText(const std::string& text);

    /// Every hint's id.
    static const std::set<std::string>& ids();

private:
    bool _enabled = true;
    std::set<std::string> _shown;
};

} // namespace fla
