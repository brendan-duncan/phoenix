#pragma once

#include <QString>

class MainWindow;

namespace commands {

/// Plays the steps in \a path on \a window as a person would (tools, clicks
/// and drags on the stage, keys, menu commands, the dialogs they open,
/// timeline cells, panels), for capture-window --steps: to try a lesson's
/// steps with no window on the screen and draw what they leave. One step a
/// line, # for a comment, words with spaces in quotes; stage points are
/// x,y in stage pixels:
///
///   tool <name>                    the toolbox's tool
///   click <x,y> [shift] [ctrl] [alt]
///   drag <x,y> <x,y> [shift] [ctrl] [alt]
///   stroke <x,y> <x,y>... [hold]    a drag through each point; with hold
///                                  the button stays down until release
///   release                        lets go of a held stroke
///   key <keys>                     "F", "Delete", "Ctrl+E", to the stage
///   menu <Modify>Deformer>...>     a menu command
///   target <spec>                  clicks a lesson target (frame:Arm:12,
///                                  layer:Tail, widget:..., menu:...);
///                                  widget:<name>@x,y clicks that point
///                                  of the widget
///   goto <frame>                   moves the playhead
///   dialog <label=value>... [picture=<out.png>] [press=<button>]
///                                  answers the next dialog: a spin box or
///                                  field by its row's label, a check box by
///                                  its text (on/off), draws it, then
///                                  presses a button
///   check <text> on|off            a check box in a panel
///   combo <panel> <text>           a panel's list, set to the item
///   pick <panel> <n>               clicks the panel's nth picture
///   show <panel>                   opens a panel by its object name
///   script <file.jsfl>             runs a script
///   capture <out.png> [x,y,w,h]    draws the window, or a part of it
///   grab <widget> <out.png>        draws one panel or widget
///   bones                          prints where armatures' joints show
///   selected [<kind>]              prints what is selected (instance,
///                                  shape, none...); fails unless <kind>
///   points                         prints a boned shape's points and bones
///   expect <text>                  fails unless the last command on the
///                                  undo list starts with the text
///
/// A dialog no step answers is reported and closed. False, with \a error
/// saying which step, when one cannot be done.
bool runCaptureSteps(MainWindow& window, const QString& path, QString& error);

} // namespace commands
