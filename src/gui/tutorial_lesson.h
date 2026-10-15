#pragma once

#include <QString>
#include <QStringList>

#include <vector>

/// An interactive tutorial: a lesson that walks through a task in Phoenix
/// one step at a time, pointing at the control to use next.
///
/// Lessons are data. Each is a folder holding lesson.json and, when it
/// starts from a document of its own, that document (an XFL folder or a
/// .phx/.fla file), which opens as an untitled copy so the reader's work is
/// never touched. Built-in lessons are in the app (:/tutorials); more can be
/// put in the tutorials folder in Phoenix's data folder.
///
/// lesson.json:
///
///     {
///       "title": "Your first animation",
///       "summary": "Draw a ball, make it a symbol and tween it.",
///       "minutes": 10,
///       "order": 1,
///       "guide": "first-animation.html",
///       "start": {"new": {"width": 1280, "height": 720, "fps": 24}},
///       "steps": [
///         {"title": "Pick the Oval tool",
///          "text": "Press <kbd>O</kbd>, or click the Oval tool.",
///          "target": "tool:Oval",
///          "wait": {"tool": "Oval"}}
///       ]
///     }
///
/// "start" is {"new": {...}} for a new document, {"document": "<path in
/// the lesson folder>"}, or absent to use the document already open.
///
/// A step's "target" (see tutorial_targets.h) is what to point at; its
/// "wait" is what moves the lesson on, so that the step asks for an action
/// rather than a click on Next:
///
/// - {"tool": "Oval"}: that tool chosen
/// - {"command": "Convert to Symbol"}: an edit of that name made (the name
///   the Edit menu's Undo shows), or one starting with it
/// - {"action": "Insert/Timeline/Keyframe"}: that menu command run
/// - {"frame": 24}: the playhead on that frame (counting from 1)
/// - {"selection": "any" | "none" | "shape" | "instance" | "text"}
/// - {"scope": "symbol" | "scene"}: inside a symbol, or back in the scene
/// - {"playing": true}: playback started
///
/// Next always moves on, so any step can be skipped.
struct TutorialStep
{
    QString title;
    QString text;
    QString target;
    /// The wait condition: its kind ("tool", "command", ...) and value.
    QString waitKind;
    QString waitValue;
    QString image;
};

struct TutorialLesson
{
    QString id;
    QString folder;
    QString title;
    QString summary;
    int minutes = 5;
    int order = 100;
    QString guide;
    /// "new", "document" or empty.
    QString startKind;
    QString startDocument;
    /// The document as the lesson leaves it, to compare with: its "final"
    /// (a file, or a folder holding DOMDocument.xml); empty when none.
    QString finalDocument;
    int startWidth = 1280;
    int startHeight = 720;
    double startFps = 24.0;
    std::vector<TutorialStep> steps;
    bool builtIn = false;
};

namespace tutorials {

/// The folder where lessons of one's own go.
QString userFolder();

/// Reads the lesson in \a folder; false, with \a error saying why, when it
/// cannot be read.
bool readLesson(const QString& folder, TutorialLesson& lesson, QString& error);

/// Every lesson, built-in and the reader's own (and those in the folder
/// PHOENIX_TUTORIALS_DIR names, for lessons being written), in order. \a problems lists
/// the lessons that could not be read.
std::vector<TutorialLesson> allLessons(QStringList* problems = nullptr);

/// How far the reader has got in a lesson: the step reached (from 0), and
/// whether it was finished.
int stepReached(const QString& id);
bool finished(const QString& id);
void recordProgress(const QString& id, int step, bool done);

} // namespace tutorials
