#pragma once

#include <string>
#include <vector>

namespace fla {

/// Reading an animatic's cut from Storyboard Pro or an editor: a CMX 3600
/// EDL or a Final Cut Pro 7 XML (xmeml), which both carry the cut as
/// events on video and audio tracks. Free of Qt.
namespace editlist {

struct Event
{
    /// The clip's name: the shot's, for a video event.
    std::string name;
    /// The file the clip came from, when the list says.
    std::string file;
    bool audio = false;
    int track = 1;
    /// Where it sits in the cut, in frames from 0: from \a start up to
    /// \a end, not including it.
    int start = 0;
    int end = 0;
    /// How far into its file it starts, in frames.
    int sourceIn = 0;
};

struct EditList
{
    std::string title;
    /// The frame rate the list counts in; 0 when it does not say (an EDL).
    double frameRate = 0.0;
    std::vector<Event> events;
};

/// Reads an EDL; its timecodes count frames at \a frameRate. Video events
/// become shots in the order of the cut, starting at the first one.
EditList parseEdl(const std::string& text, double frameRate);

/// Reads Final Cut Pro 7 XML.
EditList parseXml(const std::string& text, std::string* error = nullptr);

/// Either, by what the text looks like.
EditList parse(const std::string& text, double frameRate, std::string* error = nullptr);

/// "01:00:02:12" at \a frameRate as frames; -1 when it is not a timecode.
int timecodeFrames(const std::string& timecode, double frameRate);

/// The shot name an event's clip gives: its name without a file
/// extension, and safe as a frame label.
std::string shotName(const Event& event);

} // namespace editlist

} // namespace fla
