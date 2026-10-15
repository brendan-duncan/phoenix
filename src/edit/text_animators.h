#pragma once

#include "behaviors.h"

#include <string>
#include <vector>

namespace fla {

class StaticText;

/// Text animators: a text field's letters moved, turned, scaled, faded or
/// swapped one by one, each frame, while the text stays text you can edit.
/// Kept with the field in its phoenixTextAnimators attribute, which other
/// apps that open the FLA ignore, showing the text as typed. Built in, so
/// the stage draws them at speed. Free of Qt.
namespace textanim {

extern const char* const kAttribute;

/// The animators there are, with their settings, for the Behaviors panel.
const std::vector<behaviors::Definition>& definitions();
const behaviors::Definition* find(const std::string& id);
behaviors::Applied defaults(const std::string& id, int seed);

std::vector<behaviors::Applied> of(const StaticText& text);
bool any(const StaticText& text);

/// One letter of the field: which of all the letters, words and lines it
/// is (from 0), out of how many, and its character (a UTF-16 unit).
struct Letter
{
    int index = 0;
    int word = 0;
    int line = 0;
    int count = 1;
    int words = 1;
    int lines = 1;
    unsigned character = 0;
};

/// What the animators do to a letter: a move (pixels), a turn (degrees) and
/// a scale about its middle, an opacity factor, and a character shown in
/// its place (0 for its own).
struct Change
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scale = 1.0;
    double alpha = 1.0;
    unsigned character = 0;
    /// How far toward \a tintColor (0xRRGGBB) the letter's color goes, 0 to 1.
    double tint = 0.0;
    unsigned tintColor = 0;
    /// A blur over the letter, in pixels.
    double blur = 0.0;

    bool none() const
    {
        return x == 0 && y == 0 && rotation == 0 && scale == 1.0 && alpha == 1.0 && character == 0 && tint == 0.0 &&
            blur == 0.0;
    }
};

/// What \a list does to each of \a letters on frame \a frame of the text's
/// timeline, \a start being the first frame of the run of keyframes the
/// field is on.
std::vector<Change> apply(const std::vector<behaviors::Applied>& list, const std::vector<Letter>& letters, int frame,
    int start, double frameRate);

} // namespace textanim

} // namespace fla
