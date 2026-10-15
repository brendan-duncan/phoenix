#pragma once

#include <string>
#include <vector>

namespace fla {

class Frame;
class Layer;
class StaticText;
class Timeline;

/// Review notes: written notes on frames, and whatever a reviewer draws,
/// kept on a guide layer of their own ("Review Notes") at the top of the
/// timeline. A guide layer never exports, from Phoenix or from any other
/// app that opens the FLA, so notes travel with the file and never reach the picture. Free of Qt.
namespace review {

struct Note
{
    Frame* frame = nullptr;
    StaticText* text = nullptr;
    /// The frame it is on, from 0.
    int index = 0;
    std::string author;
    std::string body;
    bool done = false;
};

/// The timeline's notes layer, or null.
Layer* notesLayer(const Timeline& timeline);

/// A new notes layer: a guide layer named "Review Notes", marked as
/// Phoenix's, its one keyframe as long as the timeline. The caller owns
/// it until it is added.
Layer* newNotesLayer(const Timeline& timeline);

/// Every note on the timeline, in frame order.
std::vector<Note> notes(const Timeline& timeline);

/// A note's text: \a body by \a author, at \a x, \a y on the stage.
StaticText* makeNote(const std::string& author, const std::string& body, double x, double y);

/// Whether \a text is a note, and reading its parts.
bool isNote(const StaticText& text);
std::string authorOf(const StaticText& text);
bool isDone(const StaticText& text);
/// The attributes \a text would have with done set to \a done.
std::vector<std::pair<std::string, std::string>> withDone(const StaticText& text, bool done);

} // namespace review

} // namespace fla
