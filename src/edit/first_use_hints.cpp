#include "first_use_hints.h"

#include <sstream>

namespace fla {

std::optional<FirstUseHints::Hint> FirstUseHints::on(Event event, const Context& context)
{
    if (!_enabled)
        return std::nullopt;
    Hint hint;
    switch (event)
    {
    case Event::Drew:
        // Only the first drawing of an animation: on frame 1.
        if (context.frame != 0)
            return std::nullopt;
        hint = {"first-drawing",
            "Nice! For the next drawing, press F6 for a keyframe that starts as a copy of this one, "
            "or F7 for a blank one."};
        break;
    case Event::Keyframe:
        hint = {"first-keyframe",
            "A new keyframe. Turn on View > Onion Skin (Alt+Shift+O) to see the drawing before it "
            "faded under this one, and press Enter to play."};
        break;
    case Event::Symbol:
        hint = {"first-symbol",
            "It is a symbol now, kept in the Library to use again. Double-click it to draw inside it; "
            "double-click an empty spot to come back out."};
        break;
    case Event::Tween:
        if (context.tween == "motion")
            hint = {"first-tween",
                "Move the playhead to a later frame, then move or change the object there: Phoenix "
                "fills in the frames between. Press Enter to play."};
        else
            hint = {"first-tween",
                "Phoenix fills in the frames between this keyframe and the next one. Change the art "
                "on the next keyframe to see it move, and press Enter to play."};
        break;
    }
    if (_shown.count(hint.id))
        return std::nullopt;
    _shown.insert(hint.id);
    return hint;
}

std::string FirstUseHints::shownText() const
{
    std::string text;
    for (const std::string& id : _shown)
    {
        if (!text.empty())
            text += ' ';
        text += id;
    }
    return text;
}

void FirstUseHints::setShownText(const std::string& text)
{
    _shown.clear();
    std::istringstream words(text);
    std::string word;
    while (words >> word)
        if (ids().count(word))
            _shown.insert(word);
}

const std::set<std::string>& FirstUseHints::ids()
{
    static const std::set<std::string> all = {"first-drawing", "first-keyframe", "first-symbol", "first-tween"};
    return all;
}

} // namespace fla
