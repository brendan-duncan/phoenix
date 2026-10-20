#pragma once

// Phoenix in a browser page: what the page needs from Phoenix, and what
// Phoenix leaves out there. Everything here is empty on the desktop.

#include <QAction>
#include <QMenu>

#include <functional>

class QPalette;
class QWidget;

namespace web_page {

#ifdef PHOENIX_WEB
/// Sets Phoenix up for the page it runs in: menus and other pop-ups come up
/// over floating panels. Called once, before the main window shows.
void install();

/// Colors the title bars the page draws around Phoenix's own windows
/// (dialogs, floating panels) after \a palette.
void setWindowColors(const QPalette& palette);

/// Gives the commands of \a window whose keys the browser keeps for itself
/// (new, close and switch tabs and windows) keys of their own: the same
/// with Alt, or Alt and Shift, whichever is free; Ctrl+Tab's become
/// Ctrl+Alt+Page Down and Up. Called before the keys are taken as the
/// defaults, so Keyboard Shortcuts shows them.
void avoidBrowserKeys(QWidget* window);

/// What the page asks when it is about to be closed or reloaded: true
/// keeps it open long enough for the browser to ask the person.
void setUnsavedWorkCheck(std::function<bool()> check);
#endif

/// Leaves \a action out of the web build: hidden and disabled, so its
/// shortcut does nothing either. For what a browser cannot do (run other
/// programs, choose folders, quit).
inline void desktopOnly(QAction* action)
{
#ifdef PHOENIX_WEB
    if (action)
    {
        action->setVisible(false);
        action->setEnabled(false);
    }
#else
    Q_UNUSED(action);
#endif
}

inline void desktopOnly(QMenu* menu)
{
    if (menu)
        desktopOnly(menu->menuAction());
}

} // namespace web_page
